#ifndef FAMI32_I2C_H
#define FAMI32_I2C_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t fami32_i2c_init(void);
esp_err_t fami32_i2c_write(uint8_t address, const uint8_t *data, size_t length);
esp_err_t fami32_i2c_read(uint8_t address, uint8_t *data, size_t length);
esp_err_t fami32_i2c_write_read(uint8_t address,
                                const uint8_t *write_data,
                                size_t write_length,
                                uint8_t *read_data,
                                size_t read_length);

#endif /* FAMI32_I2C_H */
