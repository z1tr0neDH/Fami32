#include "fami32_battery.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "fami32_pin.h"

namespace {

constexpr const char *TAG = "Fami32Battery";
constexpr adc_unit_t kUnit = ADC_UNIT_1;
constexpr adc_channel_t kChannel = ADC_CHANNEL_7; /* GPIO8 on ESP32-S3. */
constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_0;
constexpr int kSamples = 16;

adc_oneshot_unit_handle_t s_adc = nullptr;
adc_cali_handle_t s_calibration = nullptr;

} // namespace

esp_err_t fami32_battery_init(void) {
    if (s_adc != nullptr) return ESP_OK;

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = kUnit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_config, &s_adc);
    if (err != ESP_OK) return err;

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = kAttenuation,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, kChannel, &channel_config);
    if (err != ESP_OK) return err;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = kUnit,
        .chan = kChannel,
        .atten = kAttenuation,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&calibration_config, &s_calibration);
    if (err != ESP_OK) {
        s_calibration = nullptr;
        ESP_LOGW(TAG, "eFuse calibration unavailable; using nominal ADC scale");
    }
#endif
    return ESP_OK;
}

int fami32_battery_voltage_mv(void) {
    if (s_adc == nullptr && fami32_battery_init() != ESP_OK) return -1;

    int64_t raw_sum = 0;
    int samples_read = 0;
    for (int i = 0; i < kSamples; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, kChannel, &raw) == ESP_OK) {
            raw_sum += raw;
            ++samples_read;
        }
    }
    if (samples_read == 0) return -1;

    const int raw_average = static_cast<int>(raw_sum / samples_read);
    int divider_mv = 0;
    if (s_calibration != nullptr) {
        if (adc_cali_raw_to_voltage(s_calibration, raw_average, &divider_mv) != ESP_OK) return -1;
    } else {
        /* Nominal 0 dB full scale; calibrated units take the path above. */
        divider_mv = (raw_average * 1100 + 2047) / 4095;
    }

    const int divider_total = BAT_ADC_DIVIDER_TOP_OHM + BAT_ADC_DIVIDER_BOT_OHM;
    return static_cast<int>((static_cast<int64_t>(divider_mv) * divider_total +
                             BAT_ADC_DIVIDER_BOT_OHM / 2) /
                            BAT_ADC_DIVIDER_BOT_OHM);
}

int fami32_battery_percent(void) {
    const int mv = fami32_battery_voltage_mv();
    if (mv < 0) return -1;
    if (mv <= 3300) return 0;
    if (mv >= 4200) return 100;
    return (mv - 3300) * 100 / 900;
}
