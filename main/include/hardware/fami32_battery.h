#ifndef FAMI32_BATTERY_H
#define FAMI32_BATTERY_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t fami32_battery_init(void);
int fami32_battery_voltage_mv(void);
int fami32_battery_percent(void);

#ifdef __cplusplus
}
#endif

#endif // FAMI32_BATTERY_H
