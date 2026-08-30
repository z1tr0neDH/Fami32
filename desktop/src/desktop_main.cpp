#include "desktop_platform.h"
#include "fami32_pin.h"
#include "ftm_file.h"
#include "fami32_player.h"
#include "gfx_oled_ssd1306.h"
#include "gui/gui_common.h"
#include "keypad_io.h"
#include "src_config.h"
#include "touch_input.h"
#include "git_version.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

bool _debug_print = false;
bool _midi_output = false;
bool edit_mode = false;
int g_vol = 16;

FAMI_PLAYER player;
GfxOledSSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT);
USBMIDI MIDI;
i2s_chan_handle_t i2s_tx_handle = nullptr;
TaskHandle_t SOUND_TASK_HD = nullptr;
TaskHandle_t GUI_TASK = nullptr;
TaskHandle_t KEYPAD_TASK_HD = nullptr;

namespace {

const uint8_t kKeypadRows[KEYPAD_ROWS] = {0, 1, 2, 3};
const uint8_t kKeypadCols[KEYPAD_COLS] = {0, 1, 2};

const uint8_t kBayerMatrix[4][4] = {
    {  0, 128,  32, 160},
    {192,  64, 224,  96},
    { 48, 176,  16, 144},
    {240, 112, 208,  80},
};

void draw_splash() {
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

void fade_splash(int steps, int factor, bool fade_in) {
    for (int i = 0; i < steps; ++i) {
        draw_splash();
        const int fade_value = fade_in ? i * factor : (steps - i) * factor;
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
                display.drawPixel(
                    x, y,
                    (display.getPixel(x, y) * fade_value) > kBayerMatrix[y & 3][x & 3]);
            }
        }
        display.display();
    }
}

void ensure_data_directory() {
#ifdef _WIN32
    if (_mkdir(FAMI32_STORAGE_DIR) != 0 && errno != EEXIST) perror("fami32_data");
#else
    if (mkdir(FAMI32_STORAGE_DIR, 0755) != 0 && errno != EEXIST) perror("fami32_data");
#endif
}

void create_default_config() {
    set_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
    set_config_value("ENGINE_SPEED", CONFIG_INT, &ENG_SPEED);
    set_config_value("LPF_CUTOFF", CONFIG_INT, &LPF_CUTOFF);
    set_config_value("HPF_CUTOFF", CONFIG_INT, &HPF_CUTOFF);
    set_config_value("BASE_FREQ_HZ", CONFIG_INT, &BASE_FREQ_HZ);
    set_config_value("OVER_SAMPLE", CONFIG_INT, &OVER_SAMPLE);
    set_config_value("VOLUME", CONFIG_INT, &g_vol);
    int midi_output = 0;
    int debug_print = 0;
    set_config_value("MIDI_OUT", CONFIG_INT, &midi_output);
    set_config_value("DEBUG_PRINT", CONFIG_INT, &debug_print);
    write_config(config_path);
}

void load_desktop_config() {
    if (read_config(config_path) != CONFIG_SUCCESS) {
        create_default_config();
        return;
    }
    get_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
    get_config_value("ENGINE_SPEED", CONFIG_INT, &ENG_SPEED);
    get_config_value("LPF_CUTOFF", CONFIG_INT, &LPF_CUTOFF);
    get_config_value("HPF_CUTOFF", CONFIG_INT, &HPF_CUTOFF);
    get_config_value("BASE_FREQ_HZ", CONFIG_INT, &BASE_FREQ_HZ);
    get_config_value("OVER_SAMPLE", CONFIG_INT, &OVER_SAMPLE);
    get_config_value("VOLUME", CONFIG_INT, &g_vol);
    int value = 0;
    if (get_config_value("MIDI_OUT", CONFIG_INT, &value) == CONFIG_SUCCESS) {
        _midi_output = value != 0;
    }
    if (get_config_value("DEBUG_PRINT", CONFIG_INT, &value) == CONFIG_SUCCESS) {
        _debug_print = value != 0;
    }
}

void desktop_sound_task() {
    player.reset_audio_sample_clock();
    while (!desktop_should_quit()) {
        player.process_tick();
        for (size_t i = 0; i < player.get_buf_size(); ++i) {
            player.get_buf()[i] = static_cast<int16_t>((player.get_buf()[i] * g_vol) >> 5);
        }
        desktop_audio_write(player.get_buf(), player.get_buf_size());
    }
}

} // namespace

KeypadIO keypad(reinterpret_cast<const uint8_t *>(KEYPAD_MAP),
                kKeypadRows, kKeypadCols, KEYPAD_ROWS, KEYPAD_COLS);

int main() {
    ensure_data_directory();
    if (!desktop_platform_init("Fami32", DISPLAY_WIDTH, DISPLAY_HEIGHT)) return 1;
    if (display.begin(nullptr) != ESP_OK || !keypad.begin()) return 1;

    vTaskDelay(128);
    fade_splash(32, 8, true);
    load_desktop_config();

    display.setCursor(0, 59);
    display.print("Press any key to continue...");
    display.display();
    while (!keypad.available()) {
        keypad.tick();
        vTaskDelay(2);
    }
    keypad.read();

    player.init(&ftm);
    touch_input_init();
    desktop_audio_init(SAMP_RATE);
    SOUND_TASK_HD = reinterpret_cast<TaskHandle_t>(1);
    KEYPAD_TASK_HD = reinterpret_cast<TaskHandle_t>(1);
    std::thread(desktop_sound_task).detach();

    fade_splash(16, 16, false);
    display.setFont(&rismol35);
    display.setTextColor(1);
    GUI_TASK = reinterpret_cast<TaskHandle_t>(1);
    gui_task(nullptr);
    return 0;
}
