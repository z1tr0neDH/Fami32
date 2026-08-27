#include "fami32_storage.h"

#include <cstdio>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "fami32_pin.h"
#include "sdmmc_cmd.h"

namespace {

constexpr const char *TAG = "Fami32Storage";
constexpr const char *kInternalPath = "/flash";
constexpr const char *kSdPath = "/sdcard";

sdmmc_card_t *s_card = nullptr;
bool s_sd_ready = false;

} // namespace

esp_err_t fami32_storage_init(bool sd_card_present) {
    if (s_sd_ready || !sd_card_present) {
        if (!sd_card_present) ESP_LOGI(TAG, "no SD card; using internal FAT");
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = static_cast<gpio_num_t>(SD_SCLK_GPIO);
    slot_config.cmd = static_cast<gpio_num_t>(SD_CMD_GPIO);
    slot_config.d0 = static_cast<gpio_num_t>(SD_DATA0_GPIO);
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = true,
        .use_one_fat = false,
    };

    const esp_err_t err = esp_vfs_fat_sdmmc_mount(
        kSdPath, &host, &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed (%s); using internal FAT", esp_err_to_name(err));
        s_card = nullptr;
        s_sd_ready = false;
        return err;
    }

    s_sd_ready = true;
    ESP_LOGI(TAG, "SD mounted at %s", kSdPath);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

const char *fami32_storage_dir(void) {
    return s_sd_ready ? kSdPath : kInternalPath;
}

bool fami32_sd_ready(void) { return s_sd_ready; }
