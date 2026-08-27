#ifndef FAMI32_STORAGE_H
#define FAMI32_STORAGE_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t fami32_storage_init(bool sd_card_present);
const char *fami32_storage_dir(void);
bool fami32_sd_ready(void);

#ifdef __cplusplus
}
#endif

#endif // FAMI32_STORAGE_H
