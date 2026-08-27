#include "fami32_i2c.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include "fami32_pin.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

constexpr const char *TAG = "FAMI32_I2C";
constexpr TickType_t kBusTimeout = pdMS_TO_TICKS(25);

SemaphoreHandle_t s_bus_mutex = nullptr;
bool s_initialized = false;

class BusLock {
public:
    BusLock() : locked_(s_bus_mutex != nullptr && xSemaphoreTake(s_bus_mutex, kBusTimeout) == pdTRUE) {}
    ~BusLock() {
        if (locked_) {
            xSemaphoreGive(s_bus_mutex);
        }
    }
    bool locked() const { return locked_; }

private:
    bool locked_;
};

} // namespace

esp_err_t fami32_i2c_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }

    if (s_bus_mutex == nullptr) {
        s_bus_mutex = xSemaphoreCreateMutex();
        if (s_bus_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    i2c_config_t config = {};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = static_cast<gpio_num_t>(FAMI32_I2C_SDA_GPIO);
    config.scl_io_num = static_cast<gpio_num_t>(FAMI32_I2C_SCL_GPIO);
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = FAMI32_I2C_FREQ_HZ;
    config.clk_flags = 0;

    esp_err_t err = i2c_param_config(static_cast<i2c_port_t>(FAMI32_I2C_PORT), &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(static_cast<i2c_port_t>(FAMI32_I2C_PORT), I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "I2C0 ready: SCL=%d SDA=%d @ %d Hz",
             FAMI32_I2C_SCL_GPIO, FAMI32_I2C_SDA_GPIO, FAMI32_I2C_FREQ_HZ);
    return ESP_OK;
}

esp_err_t fami32_i2c_write(uint8_t address, const uint8_t *data, size_t length) {
    if (!s_initialized) {
        esp_err_t err = fami32_i2c_init();
        if (err != ESP_OK) return err;
    }
    if (data == nullptr || length == 0) return ESP_ERR_INVALID_ARG;

    BusLock lock;
    if (!lock.locked()) return ESP_ERR_TIMEOUT;
    return i2c_master_write_to_device(static_cast<i2c_port_t>(FAMI32_I2C_PORT),
                                      address, data, length, kBusTimeout);
}

esp_err_t fami32_i2c_read(uint8_t address, uint8_t *data, size_t length) {
    if (!s_initialized) {
        esp_err_t err = fami32_i2c_init();
        if (err != ESP_OK) return err;
    }
    if (data == nullptr || length == 0) return ESP_ERR_INVALID_ARG;

    BusLock lock;
    if (!lock.locked()) return ESP_ERR_TIMEOUT;
    return i2c_master_read_from_device(static_cast<i2c_port_t>(FAMI32_I2C_PORT),
                                       address, data, length, kBusTimeout);
}

esp_err_t fami32_i2c_write_read(uint8_t address,
                                const uint8_t *write_data,
                                size_t write_length,
                                uint8_t *read_data,
                                size_t read_length) {
    if (!s_initialized) {
        esp_err_t err = fami32_i2c_init();
        if (err != ESP_OK) return err;
    }
    if (write_data == nullptr || write_length == 0 || read_data == nullptr || read_length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    BusLock lock;
    if (!lock.locked()) return ESP_ERR_TIMEOUT;
    return i2c_master_write_read_device(static_cast<i2c_port_t>(FAMI32_I2C_PORT),
                                        address,
                                        write_data,
                                        write_length,
                                        read_data,
                                        read_length,
                                        kBusTimeout);
}
