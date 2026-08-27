#include <cstdint>
#include <cstring>
#include <vector>

#include "keypad_io.h"
#include <gfx_oled_ssd1306.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include "fami32_pin.h"
#include "fami32_factory_test.h"
#include "fami32_storage.h"
#include "nau88c22.h"
#include "ftm_file.h"
#include "fami32_player.h"
#include <dirent.h>
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "gui/gui_common.h"
#include "gui/gui_input.h"
#include "boot_check.h"
#include "touch_input.h"
#include "git_version.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "tinyusb_helper.h"
#include "boot_router.h"
#include <USBMIDI.h>

bool _debug_print = false;
bool _midi_output = false;
bool edit_mode = false;

int g_vol = 16;

extern "C" {
#include "micro_config.h"
}

FAMI_PLAYER player;
i2s_chan_handle_t i2s_tx_handle = nullptr;
static i2s_chan_handle_t codec_i2s_tx_handle = nullptr;
TaskHandle_t SOUND_TASK_HD = NULL;
TaskHandle_t GUI_TASK = NULL;
TaskHandle_t KEYPAD_TASK_HD = NULL;
USBMIDI MIDI;
esp_lcd_panel_handle_t panel;
GfxOledSSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT);
KeypadIO keypad;

static constexpr size_t MIDI_TIMELINE_QUEUE_SIZE = 256;
static constexpr int64_t MIDI_TIMELINE_LATENCY_MARGIN_US = 3000;

typedef struct {
    uint64_t sample_time;
    uint32_t sequence;
    midiEventPacket_t packet;
} midi_timeline_event_t;

static portMUX_TYPE midi_timeline_lock = portMUX_INITIALIZER_UNLOCKED;
static midi_timeline_event_t midi_timeline_queue[MIDI_TIMELINE_QUEUE_SIZE];
static size_t midi_timeline_count = 0;
static uint32_t midi_timeline_sequence = 0;
static int64_t midi_timeline_origin_us = 0;

static void handle_midi_packet(midiEventPacket_t packet);

static uint64_t midi_timeline_us_to_samples(int64_t us) {
    if (us <= 0) {
        return 0;
    }
    return ((uint64_t)us * (uint64_t)SAMP_RATE + 500000ULL) / 1000000ULL;
}

static int64_t midi_timeline_latency_us() {
    int engine_speed = ENG_SPEED;
    if (engine_speed < 1) {
        engine_speed = 1;
    }
    return (1000000LL / engine_speed) + MIDI_TIMELINE_LATENCY_MARGIN_US;
}

static void midi_timeline_reset() {
    portENTER_CRITICAL(&midi_timeline_lock);
    midi_timeline_origin_us = esp_timer_get_time();
    midi_timeline_count = 0;
    midi_timeline_sequence = 0;
    portEXIT_CRITICAL(&midi_timeline_lock);
}

static bool midi_timeline_is_earlier(const midi_timeline_event_t &a, const midi_timeline_event_t &b) {
    if (a.sample_time != b.sample_time) {
        return a.sample_time < b.sample_time;
    }
    return a.sequence < b.sequence;
}

static void midi_timeline_push(midiEventPacket_t packet) {
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&midi_timeline_lock);
    if (midi_timeline_origin_us == 0) {
        midi_timeline_origin_us = now_us;
    }

    midi_timeline_event_t event = {
        .sample_time = midi_timeline_us_to_samples((now_us - midi_timeline_origin_us) + midi_timeline_latency_us()),
        .sequence = midi_timeline_sequence++,
        .packet = packet,
    };

    if (midi_timeline_count < MIDI_TIMELINE_QUEUE_SIZE) {
        midi_timeline_queue[midi_timeline_count++] = event;
    } else {
        size_t drop_index = 0;
        for (size_t i = 1; i < midi_timeline_count; i++) {
            if (midi_timeline_is_earlier(midi_timeline_queue[i], midi_timeline_queue[drop_index])) {
                drop_index = i;
            }
        }
        midi_timeline_queue[drop_index] = event;
    }
    portEXIT_CRITICAL(&midi_timeline_lock);
}

static bool midi_timeline_next_event(uint64_t window_start_sample,
                                     uint64_t window_end_sample,
                                     uint64_t *event_sample,
                                     void *user) {
    (void)user;

    bool found = false;
    uint64_t best_sample = 0;
    uint32_t best_sequence = 0;

    portENTER_CRITICAL(&midi_timeline_lock);
    for (size_t i = 0; i < midi_timeline_count; i++) {
        const midi_timeline_event_t &event = midi_timeline_queue[i];
        if (event.sample_time < window_start_sample || event.sample_time >= window_end_sample) {
            continue;
        }
        if (!found ||
            event.sample_time < best_sample ||
            (event.sample_time == best_sample && event.sequence < best_sequence)) {
            found = true;
            best_sample = event.sample_time;
            best_sequence = event.sequence;
        }
    }
    portEXIT_CRITICAL(&midi_timeline_lock);

    if (found && event_sample != NULL) {
        *event_sample = best_sample;
    }
    return found;
}

static bool midi_timeline_pop_due(uint64_t sample_time, midiEventPacket_t *packet) {
    bool found = false;
    size_t best_index = 0;

    portENTER_CRITICAL(&midi_timeline_lock);
    for (size_t i = 0; i < midi_timeline_count; i++) {
        if (midi_timeline_queue[i].sample_time > sample_time) {
            continue;
        }
        if (!found || midi_timeline_is_earlier(midi_timeline_queue[i], midi_timeline_queue[best_index])) {
            found = true;
            best_index = i;
        }
    }

    if (found) {
        *packet = midi_timeline_queue[best_index].packet;
        midi_timeline_count--;
        if (best_index < midi_timeline_count) {
            midi_timeline_queue[best_index] = midi_timeline_queue[midi_timeline_count];
        }
    }
    portEXIT_CRITICAL(&midi_timeline_lock);

    return found;
}

static void midi_timeline_dispatch_due(uint64_t sample_time, void *user) {
    (void)user;

    midiEventPacket_t packet;
    while (midi_timeline_pop_due(sample_time, &packet)) {
        handle_midi_packet(packet);
    }
}

static esp_err_t init_i2s_output(i2s_port_t port,
                                 gpio_num_t mclk,
                                 gpio_num_t bclk,
                                 gpio_num_t ws,
                                 gpio_num_t dout,
                                 i2s_chan_handle_t *handle) {
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&channel_config, handle, nullptr);
    if (err != ESP_OK) return err;

    i2s_std_clk_config_t clock_config = I2S_STD_CLK_DEFAULT_CONFIG(48000);
    clock_config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    const i2s_std_config_t standard_config = {
        .clk_cfg = clock_config,
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    if ((err = i2s_channel_init_std_mode(*handle, &standard_config)) != ESP_OK) return err;
    return i2s_channel_enable(*handle);
}

void sound_task(void *arg) {
    (void)arg;
    player.init(&ftm);

    const bool codec_ready = fami32_codec_init() == ESP_OK;
    ESP_ERROR_CHECK(init_i2s_output(
        static_cast<i2s_port_t>(SPEAKER_I2S_PORT),
        I2S_GPIO_UNUSED,
        static_cast<gpio_num_t>(SPEAKER_I2S_BCLK_GPIO),
        static_cast<gpio_num_t>(SPEAKER_I2S_WS_GPIO),
        static_cast<gpio_num_t>(SPEAKER_I2S_DOUT_GPIO),
        &i2s_tx_handle));
    if (codec_ready) {
        ESP_ERROR_CHECK(init_i2s_output(
            static_cast<i2s_port_t>(CODEC_I2S_PORT),
            static_cast<gpio_num_t>(CODEC_I2S_MCLK_GPIO),
            static_cast<gpio_num_t>(CODEC_I2S_BCLK_GPIO),
            static_cast<gpio_num_t>(CODEC_I2S_WS_GPIO),
            static_cast<gpio_num_t>(CODEC_I2S_DOUT_GPIO),
            &codec_i2s_tx_handle));
    }

    std::vector<int16_t> stereo_buffer(player.get_buf_size() * 2, 0);
    size_t written = 0;
    i2s_channel_write(i2s_tx_handle, stereo_buffer.data(),
                      stereo_buffer.size() * sizeof(int16_t), &written, portMAX_DELAY);
    if (codec_i2s_tx_handle != nullptr) {
        i2s_channel_write(codec_i2s_tx_handle, stereo_buffer.data(),
                          stereo_buffer.size() * sizeof(int16_t), &written, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_ERROR_CHECK(fami32_codec_set_muted(false));
    }
    keypad.setAudioReady(true);

    player.reset_audio_sample_clock();
    midi_timeline_reset();

    for (;;) {
        player.process_tick(midi_timeline_next_event, midi_timeline_dispatch_due, NULL);
        for (int i = 0; i < player.get_buf_size(); i++) {
            int32_t sample = (static_cast<int32_t>(player.get_buf()[i]) * g_vol) >> 5;
            if (sample > INT16_MAX) sample = INT16_MAX;
            if (sample < INT16_MIN) sample = INT16_MIN;
            stereo_buffer[i * 2] = static_cast<int16_t>(sample);
            stereo_buffer[i * 2 + 1] = static_cast<int16_t>(sample);
        }
        i2s_channel_write(i2s_tx_handle, stereo_buffer.data(),
                          stereo_buffer.size() * sizeof(int16_t), &written, portMAX_DELAY);
        if (codec_i2s_tx_handle != nullptr) {
            i2s_channel_write(codec_i2s_tx_handle, stereo_buffer.data(),
                              stereo_buffer.size() * sizeof(int16_t), &written, portMAX_DELAY);
        }
    }
}

void keypad_task(void *arg) {
    (void)arg;
    for (;;) {
        keypad.tick();
        const int volume_delta = keypad.takeVolumeDelta();
        if (volume_delta != 0) {
            g_vol += volume_delta;
            if (g_vol < 0) g_vol = 0;
            if (g_vol > 64) g_vol = 64;
            ESP_LOGI("Fami32Volume", "volume=%d", g_vol);
        }
        vTaskDelay(2);
    }
}

esp_lcd_panel_handle_t oled_init(void)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;

    gpio_config_t power_config = {};
    power_config.pin_bit_mask = (1ULL << DISPLAY_POWER_EN);
    power_config.mode = GPIO_MODE_OUTPUT;
    power_config.pull_up_en = GPIO_PULLUP_DISABLE;
    power_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    power_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&power_config));
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(DISPLAY_POWER_EN), 1));
    vTaskDelay(pdMS_TO_TICKS(20));

    spi_bus_config_t buscfg = {
        .mosi_io_num = DISPLAY_SDA,
        .miso_io_num = -1,
        .sclk_io_num = DISPLAY_SCL,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT / 8,
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO)
    );

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = DISPLAY_CS,
        .dc_gpio_num = DISPLAY_DC,
        .spi_mode = 0,
        .pclk_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = {
            .dc_low_on_param = 1,
        },
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)SPI2_HOST,
            &io_config,
            &io_handle
        )
    );

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = DISPLAY_HEIGHT,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = DISPLAY_RESET,
        .bits_per_pixel = 1,
        .vendor_config = &ssd1306_config,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_ssd1306(
            io_handle,
            &panel_config,
            &panel_handle
        )
    );

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    /* The installed OLED is physically opposite to the previous controller
     * orientation.  Flipping both axes rotates the visible image 180 degrees
     * relative to the previous firmware. */
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    return panel_handle;
}

const uint8_t bayerMatrix[4][4] = {
    {  0, 128,  32, 160 },
    { 192,  64, 224,  96 },
    {  48, 176,  16, 144 },
    { 240, 112, 208,  80 }
};

void drawFami32Splash(GfxOledSSD1306 &display) {
    display.fillScreen(1);
    display.drawBitmap(48, 1, fami32_logo, 32, 32, 0);
    display.setTextColor(0);
    display.setFont(&rismol57);
    display.setCursor(47, 33);
    display.print("FAMI32");
    display.setFont(&rismol35);
    display.setCursor(0, 0);
    display.printf("FM32-%s", get_version_string());
    display.setCursor(0, 47);
    display.printf("By libchara-dev\n%s %s", __DATE__, __TIME__);
}

void bayerDitherFade(GfxOledSSD1306 &display, int steps, int factor, bool fadeIn) {
    for (int i = 0; i < steps; i++) {
        drawFami32Splash(display);
        int fadeValue = fadeIn ? (i * factor) : ((steps - i) * factor);
        for (int x = 0; x < 128; x++) {
            for (int y = 0; y < 64; y++) {
                display.drawPixel(x, y, (display.getPixel(x, y) * fadeValue) > bayerMatrix[y & 3][x & 3]);
            }
        }
        display.display();
    }
}

static void handle_midi_packet(midiEventPacket_t packet) {
    midiEventData_t e;
    memcpy(&e, &packet, 4);
    if (_debug_print) {
        ESP_LOGI("MIDI_CALLBACK", "%X%X %X%X %02X %02X", e.cn, e.cin, e.ch, e.event, e.note, e.vol);
    }
    const bool note_on = e.event == MIDI_CIN_NOTE_ON && e.vol > 0;
    const bool note_off = e.event == MIDI_CIN_NOTE_OFF || (e.event == MIDI_CIN_NOTE_ON && e.vol == 0);
    if (edit_mode) {
        set_channel_sel_pos(e.ch);
    }

    if (note_on) {
        if (edit_mode) {
            player.channel[channel_sel_pos].set_inst(inst_sel_pos);
            player.channel[channel_sel_pos].set_note(e.note);
            player.channel[channel_sel_pos].set_vol(e.vol >> 3);
            player.channel[channel_sel_pos].note_start();
            unpk_item_t pt_tmp = ftm.get_pt_item(channel_sel_pos, player.get_cur_frame_map(channel_sel_pos), player.get_row());
            pt_tmp.note = (e.note % 12) + 1;
            pt_tmp.octave = (e.note / 12) - 2;
            pt_tmp.instrument = inst_sel_pos;
            pt_tmp.volume = e.vol >> 3;
            ftm.set_pt_item(channel_sel_pos, player.get_cur_frame_map(channel_sel_pos), player.get_row(), pt_tmp);
            if (!player.get_play_status()) {
                player.set_row(player.get_row() + 1);
            }
        } else {
            note_io_preview_note_on(e.note, e.vol >> 3, NOTE_IO_SOURCE_MIDI, e.ch);
        }
    } else if (note_off) {
        if (edit_mode) {
            player.channel[channel_sel_pos].note_end();
            if (player.get_play_status()) {
                unpk_item_t pt_tmp = ftm.get_pt_item(channel_sel_pos, player.get_cur_frame_map(channel_sel_pos), player.get_row());
                pt_tmp.note = NOTE_END;
                pt_tmp.instrument = NO_INST;
                pt_tmp.octave = NO_OCT;
                ftm.set_pt_item(channel_sel_pos, player.get_cur_frame_map(channel_sel_pos), player.get_row(), pt_tmp);
            }
        } else {
            note_io_preview_note_off(e.note, NOTE_IO_SOURCE_MIDI, e.ch);
        }
    } else if (e.event == MIDI_CIN_CONTROL_CHANGE) {
        if (e.note == 0x20) {
            uint8_t set_prog = e.vol;
            if (set_prog >= ftm.inst_block.inst_num) {
                set_prog = ftm.inst_block.inst_num - 1;
            }
            inst_sel_pos = set_prog;
        } else if (e.note == 0x7B) {
            if (edit_mode) {
                player.channel[channel_sel_pos].note_cut();
            } else {
                note_io_preview_all_notes_off();
            }
            printf("ALL NOTES OFF\n");
        } else {
            printf("UNKNOW CC: CMD=%02X, PARAM=%02X\n", e.note, e.vol);
        }
    } else if (e.event == MIDI_CIN_PITCH_BEND_CHANGE) {
        BASE_FREQ_HZ = 440 + (1024 - (((e.vol << 8) | e.note) / 16));
        printf("FINETUNE: %d, PITCH_BEND: 0x%04X\n", BASE_FREQ_HZ, (e.vol << 8) | e.note);
    } else {
        player.channel[channel_sel_pos].note_cut();
        printf("UNKNOW USB MIDI EVENT: 0x%X (%d) -> DATA=%02X %02X\n", e.event, e.event, e.note, e.vol);
    }
}

void midi_callback(midiEventPacket_t packet) {
    midi_timeline_push(packet);
}

#define CONFIG_TOTAL_LEN_MIDI (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

TU_ATTR_ALIGNED(4)
uint8_t const usb_cfg_desc_midi[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, CONFIG_TOTAL_LEN_MIDI, 0x80, 100),
    TUD_MIDI_DESCRIPTOR(0, 4, 0x01, 0x81, 64),
};

extern "C" void app_main(void) {
    touch_input_init();
    panel = oled_init();
    display.begin(panel);
    ESP_ERROR_CHECK(keypad.begin() ? ESP_OK : ESP_FAIL);
    for (int i = 0; i < 5; ++i) {
        keypad.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    /* Consume a routed restart before interpreting the same held boot keys. */
    boot_router();
    if (boot_check()) show_check_info(&display, &keypad);
    if (keypad.volumeUpPressed() && keypad.volumeDownPressed()) {
        fami32_factory_test(&display, &keypad);
    }
    if (keypad.volumeDownPressed() || keypad.isPressed(KEY_BACK)) {
        boot_router_set_mode(USB_MSC);
    }
    keypad.clear();
    
    vTaskDelay(128);

    bayerDitherFade(display, 32, 8, true);

    esp_vfs_fat_mount_config_t fat_conf = {
        .format_if_mount_failed = true,
        .max_files = 4
    };
    wl_handle_t wl_flash_handle;
    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl("/flash", "flash", &fat_conf, &wl_flash_handle);
    printf("\nFatFS mount %d: %s\n", ret, esp_err_to_name(ret));
    fami32_storage_init(keypad.sdCardPresent());

    if (read_config(config_path) != CONFIG_SUCCESS) {
        display.setCursor(0, 59);
        display.printf("Create new config...");
        display.display();
        printf("NO CONFIG FILE FOUND.\nCREATE IT...\n");
        set_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
        set_config_value("ENGINE_SPEED", CONFIG_INT, &ENG_SPEED);
        set_config_value("LPF_CUTOFF", CONFIG_INT, &LPF_CUTOFF);
        set_config_value("HPF_CUTOFF", CONFIG_INT, &HPF_CUTOFF);
        set_config_value("BASE_FREQ_HZ", CONFIG_INT, &BASE_FREQ_HZ);
        set_config_value("OVER_SAMPLE", CONFIG_INT, &OVER_SAMPLE);
        set_config_value("VOLUME", CONFIG_INT, &g_vol);
        int midi_output = _midi_output ? 1 : 0;
        int debug_print = _debug_print ? 1 : 0;
        set_config_value("MIDI_OUT", CONFIG_INT, &midi_output);
        set_config_value("DEBUG_PRINT", CONFIG_INT, &debug_print);
        if (write_config(config_path) != CONFIG_SUCCESS) {
            printf("Failed to write config file.\n");
        }
        esp_restart();
    }

    get_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
    get_config_value("ENGINE_SPEED", CONFIG_INT, &ENG_SPEED);
    get_config_value("LPF_CUTOFF", CONFIG_INT, &LPF_CUTOFF);
    get_config_value("HPF_CUTOFF", CONFIG_INT, &HPF_CUTOFF);
    get_config_value("BASE_FREQ_HZ", CONFIG_INT, &BASE_FREQ_HZ);
    get_config_value("OVER_SAMPLE", CONFIG_INT, &OVER_SAMPLE);
    get_config_value("VOLUME", CONFIG_INT, &g_vol);
    bool config_migrated = false;
    if (SAMP_RATE != 48000) {
        SAMP_RATE = 48000;
        set_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
        config_migrated = true;
    }
    if (g_vol < 0 || g_vol > 64) {
        g_vol = g_vol < 0 ? 0 : 64;
        set_config_value("VOLUME", CONFIG_INT, &g_vol);
        config_migrated = true;
    }
    int midi_output = _midi_output ? 1 : 0;
    if (get_config_value("MIDI_OUT", CONFIG_INT, &midi_output) == CONFIG_SUCCESS) {
        _midi_output = midi_output != 0;
    } else {
        set_config_value("MIDI_OUT", CONFIG_INT, &midi_output);
        config_migrated = true;
    }

    int debug_print = _debug_print ? 1 : 0;
    if (get_config_value("DEBUG_PRINT", CONFIG_INT, &debug_print) == CONFIG_SUCCESS) {
        _debug_print = debug_print != 0;
    } else {
        set_config_value("DEBUG_PRINT", CONFIG_INT, &debug_print);
        config_migrated = true;
    }

    if (config_migrated) {
        write_config(config_path);
    }

    init_tinyusb(usb_cfg_desc_midi, sizeof(usb_cfg_desc_midi));
    MIDI.begin();

    display.setCursor(0, 59);
    display.printf("Press any key to continue...");
    display.display();

    while (!keypad.available()) {
        keypad.tick();
        vTaskDelay(2);
    }
    keypad.read();

    xTaskCreatePinnedToCore(keypad_task, "KEYPAD", 8192, NULL, 4, &KEYPAD_TASK_HD, 1);
    xTaskCreatePinnedToCore(sound_task, "SOUND TASK", 8192, NULL, 20, &SOUND_TASK_HD, 0);

    MIDI.setCallback(midi_callback);

    bayerDitherFade(display, 16, 16, false);

    display.setFont(&rismol35);
    display.setTextColor(1);

    xTaskCreatePinnedToCore(gui_task, "GUI", 16384, NULL, 2, &GUI_TASK, 1);
}
