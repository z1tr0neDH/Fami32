#ifndef NAU88C22_H
#define NAU88C22_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t fami32_codec_init(void);
esp_err_t fami32_codec_set_muted(bool muted);
bool fami32_codec_ready(void);
uint16_t fami32_codec_device_id(void);

#ifdef __cplusplus
}
#endif

#endif // NAU88C22_H
