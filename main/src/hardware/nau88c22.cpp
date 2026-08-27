#include "nau88c22.h"

#include "esp_log.h"
#include "fami32_i2c.h"
#include "fami32_pin.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char *TAG = "NAU88C22";
constexpr uint8_t kRegReset = 0;
constexpr uint8_t kRegPower1 = 1;
constexpr uint8_t kRegPower2 = 2;
constexpr uint8_t kRegPower3 = 3;
constexpr uint8_t kRegAudioInterface = 4;
constexpr uint8_t kRegClock1 = 6;
constexpr uint8_t kRegDacControl = 10;
constexpr uint8_t kRegLeftDacVolume = 11;
constexpr uint8_t kRegRightDacVolume = 12;
constexpr uint8_t kRegLeftMixer = 50;
constexpr uint8_t kRegRightMixer = 51;
constexpr uint8_t kRegLeftHeadphoneVolume = 52;
constexpr uint8_t kRegRightHeadphoneVolume = 53;
constexpr uint8_t kRegDeviceId = 63;
constexpr uint16_t kExpectedDeviceId = 0x01A;

bool s_ready = false;
uint16_t s_device_id = 0;

esp_err_t write_register(uint8_t reg, uint16_t value) {
    if (reg > 0x7F || value > 0x1FF) return ESP_ERR_INVALID_ARG;

    /* The NAU88C22 2-wire write frame is 16 bits, not a separate
     * register byte followed by a 16-bit value.  The first byte contains
     * the seven-bit register address and data bit 8. */
    const uint8_t payload[2] = {
        static_cast<uint8_t>((reg << 1) | ((value >> 8) & 0x01)),
        static_cast<uint8_t>(value & 0xFF),
    };
    return fami32_i2c_write(NAU88C22_I2C_ADDRESS, payload, sizeof(payload));
}

esp_err_t read_register(uint8_t reg, uint16_t *value) {
    if (reg > 0x7F || value == nullptr) return ESP_ERR_INVALID_ARG;

    /* For a read, select the register with the same shifted seven-bit
     * control address used by the write framing, then read its 9-bit value. */
    const uint8_t address = static_cast<uint8_t>(reg << 1);
    uint8_t payload[2] = {};
    const esp_err_t err = fami32_i2c_write_read(
        NAU88C22_I2C_ADDRESS, &address, 1, payload, sizeof(payload));
    if (err == ESP_OK) {
        *value = static_cast<uint16_t>(((payload[0] & 0x01) << 8) | payload[1]);
    }
    return err;
}

esp_err_t write_checked(uint8_t reg, uint16_t value) {
    const esp_err_t err = write_register(reg, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write R%u=0x%03X failed: %s",
                 reg, value, esp_err_to_name(err));
    }
    return err;
}

} // namespace

esp_err_t fami32_codec_init(void) {
    s_ready = false;
    s_device_id = 0;

    esp_err_t err = fami32_i2c_init();
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(2));
    err = write_checked(kRegReset, 0x000);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(2));

    err = read_register(kRegDeviceId, &s_device_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "device ID read failed: %s", esp_err_to_name(err));
        return err;
    }
    if (s_device_id != kExpectedDeviceId) {
        ESP_LOGE(TAG, "unexpected device ID 0x%03X (expected 0x%03X)",
                 s_device_id, kExpectedDeviceId);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Enable reference/analog bias with 80 kOhm impedance for a low-pop ramp. */
    if ((err = write_checked(kRegPower1, 0x00D)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(250));

    /* Slave, MCLK/1, 16-bit Philips I2S. */
    if ((err = write_checked(kRegClock1, 0x000)) != ESP_OK) return err;
    if ((err = write_checked(kRegAudioInterface, 0x010)) != ESP_OK) return err;

    /* Power both DACs, output mixers and headphone drivers. */
    if ((err = write_checked(kRegPower2, 0x180)) != ESP_OK) return err;
    if ((err = write_checked(kRegPower3, 0x00F)) != ESP_OK) return err;

    /* Start muted; full-scale digital path, DAC routed to both HP mixers. */
    if ((err = write_checked(kRegDacControl, 0x040)) != ESP_OK) return err;
    if ((err = write_checked(kRegLeftDacVolume, 0x0FF)) != ESP_OK) return err;
    if ((err = write_checked(kRegRightDacVolume, 0x1FF)) != ESP_OK) return err;
    if ((err = write_checked(kRegLeftMixer, 0x001)) != ESP_OK) return err;
    if ((err = write_checked(kRegRightMixer, 0x001)) != ESP_OK) return err;
    if ((err = write_checked(kRegLeftHeadphoneVolume, 0x079)) != ESP_OK) return err;
    if ((err = write_checked(kRegRightHeadphoneVolume, 0x179)) != ESP_OK) return err;

    s_ready = true;
    ESP_LOGI(TAG, "ready: ID=0x%03X, 48 kHz/16-bit I2S slave", s_device_id);
    return ESP_OK;
}

esp_err_t fami32_codec_set_muted(bool muted) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = write_checked(kRegDacControl, muted ? 0x040 : 0x000);
    if (err != ESP_OK) return err;
    err = write_checked(kRegLeftHeadphoneVolume, muted ? 0x079 : 0x039);
    if (err != ESP_OK) return err;
    return write_checked(kRegRightHeadphoneVolume, muted ? 0x179 : 0x139);
}

bool fami32_codec_ready(void) { return s_ready; }
uint16_t fami32_codec_device_id(void) { return s_device_id; }
