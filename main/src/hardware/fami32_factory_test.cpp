#include "fami32_factory_test.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "fami32_battery.h"
#include "fami32_storage.h"
#include "fonts/rismol_3_5.h"
#include "fonts/rismol_5_7.h"
#include "gfx_oled_ssd1306.h"
#include "keypad_io.h"
#include "nau88c22.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char *TAG = "Fami32Factory";

void draw_header(GfxOledSSD1306 *display, const char *title) {
    display->clearDisplay();
    display->fillRect(0, 0, 128, 9, 1);
    display->setFont(&rismol57);
    display->setTextColor(0);
    display->setCursor(1, 1);
    display->print(title);
    display->setFont(&rismol35);
    display->setTextColor(1);
    display->setCursor(0, 11);
}

} // namespace

void fami32_factory_test(GfxOledSSD1306 *display, KeypadIO *keypad) {
    if (display == nullptr || keypad == nullptr) return;

    const esp_err_t battery_result = fami32_battery_init();
    const esp_err_t codec_result = fami32_codec_init();
    const esp_err_t sd_result = fami32_storage_init(keypad->sdCardPresent());
    ESP_LOGI(TAG, "start: PCF=%d codec=%s ID=0x%03X SD=%s battery=%s",
             keypad->pcfReady(), esp_err_to_name(codec_result), fami32_codec_device_id(),
             esp_err_to_name(sd_result), esp_err_to_name(battery_result));

    /* Require release after the boot chord so page controls start cleanly. */
    while (keypad->volumeUpPressed() || keypad->volumeDownPressed()) {
        keypad->tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    keypad->clear();

    uint8_t page = 0;
    uint64_t previous_matrix = UINT64_MAX;
    int32_t previous_encoder_0 = INT32_MIN;
    int32_t previous_encoder_1 = INT32_MIN;

    for (;;) {
        keypad->tick();
        while (keypad->available()) {
            const keypadEvent event = keypad->read();
            if (event.bit.EVENT != KEY_JUST_PRESSED) continue;
            if (event.bit.KEY == KEY_OK) page = static_cast<uint8_t>((page + 1) % 3);
            if (event.bit.KEY == KEY_BACK) {
                draw_header(display, "FACTORY TEST");
                display->print("REBOOT...");
                display->display();
                vTaskDelay(pdMS_TO_TICKS(250));
                esp_restart();
            }
        }

        const uint64_t matrix = keypad->rawMatrixState();
        const int32_t encoder_0 = keypad->encoderPosition(0);
        const int32_t encoder_1 = keypad->encoderPosition(1);
        if (matrix != previous_matrix || encoder_0 != previous_encoder_0 ||
            encoder_1 != previous_encoder_1) {
            ESP_LOGI(TAG,
                     "matrix=%09" PRIX64 " enc1=%" PRId32 " enc2=%" PRId32
                     " port=%04X vol-=%d vol+=%d hp=%d",
                     matrix, encoder_0, encoder_1, keypad->portSnapshot(),
                     keypad->volumeDownPressed(), keypad->volumeUpPressed(),
                     keypad->headphonesInserted());
            previous_matrix = matrix;
            previous_encoder_0 = encoder_0;
            previous_encoder_1 = encoder_1;
        }

        if (page == 0) {
            draw_header(display, "FACTORY 1/3");
            display->printf("PCF8575: %s\n", keypad->pcfReady() ? "PASS" : "FAIL");
            display->printf("NAU ID: %03X %s\n", fami32_codec_device_id(),
                            codec_result == ESP_OK ? "PASS" : "FAIL");
            display->printf("SD: %s  HP:%s\n", fami32_sd_ready() ? "PASS" : "--",
                            keypad->headphonesInserted() ? "IN" : "OUT");
            display->printf("BAT: %dmV %d%%\n", fami32_battery_voltage_mv(),
                            fami32_battery_percent());
            display->print("CHG LED: HARDWARE");
        } else if (page == 1) {
            draw_header(display, "MATRIX 2/3");
            for (int row = 0; row < 6; ++row) {
                const unsigned row_bits = static_cast<unsigned>((matrix >> (row * 6)) & 0x3F);
                display->printf("H%d: %02X%s", row + 1, row_bits, (row & 1) ? "\n" : "  ");
            }
            display->print("OK:NEXT BACK:EXIT");
        } else {
            draw_header(display, "INPUT 3/3");
            display->printf("ENC1:%" PRId32 " ENC2:%" PRId32 "\n", encoder_0, encoder_1);
            display->printf("VOL-:%d VOL+:%d\n", keypad->volumeDownPressed(),
                            keypad->volumeUpPressed());
            display->printf("HP:%s SD:%s\n", keypad->headphonesInserted() ? "IN" : "OUT",
                            keypad->sdCardPresent() ? "IN" : "OUT");
            display->printf("PORT:%04X\n", keypad->portSnapshot());
            display->print("OK:NEXT BACK:EXIT");
        }
        display->display();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
