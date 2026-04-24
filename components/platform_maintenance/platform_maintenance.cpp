/****************************************************************************
 * Kiri Bridge
 * CN105 HomeKit controller for Mitsubishi heat pumps
 * https://kiri.dkt.moe
 * https://github.com/DickyT/kiri-homekit
 *
 * Copyright (c) 2026
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 ****************************************************************************/

#include "platform_maintenance.h"

#include "esp_log.h"
#include "esp_system.h"
#include "hap.h"
#include "nvs_flash.h"
#include "platform_fs.h"
#include "platform_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>

namespace {

const char* TAG = "platform_maintenance";

void setResult(platform_maintenance::Result& result, const char* action, bool ok, bool rebooting, const char* message) {
    result.ok = ok;
    result.rebooting = rebooting;
    std::snprintf(result.action, sizeof(result.action), "%s", action == nullptr ? "" : action);
    std::snprintf(result.message, sizeof(result.message), "%s", message == nullptr ? "" : message);
}

void rebootTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
}

}  // namespace

namespace platform_maintenance {

Result resetHomeKit() {
    Result result{};
    const int ret = hap_reset_homekit_data();
    setResult(result,
              "reset-homekit",
              ret == HAP_SUCCESS,
              ret == HAP_SUCCESS,
              ret == HAP_SUCCESS
                  ? "HomeKit reset requested. The HomeKit SDK will clear pairing/accessory/network data and reboot."
                  : "HomeKit SDK rejected reset request.");
    return result;
}

Result clearLogs() {
    Result result{};
    const bool ok = platform_log::clearCurrentLog("manual current log clear");
    setResult(result,
              "clear-logs",
              ok,
              false,
              ok ? "Current log cleared. Logging will continue in the same file."
                 : "Failed to clear current log.");
    return result;
}

Result clearSpiffs() {
    Result result{};
    const platform_log::Status log = platform_log::getStatus();
    char fs_message[256] = {};
    const bool removed = platform_fs::removeAllFilesExcept(log.currentPath, fs_message, sizeof(fs_message));
    const bool current_cleared = platform_log::clearCurrentLog("manual SPIFFS clear");

    char message[512] = {};
    std::snprintf(message,
                  sizeof(message),
                  "%s; current_log_cleared=%s",
                  fs_message,
                  current_cleared ? "true" : "false");
    setResult(result, "clear-spiffs", removed && current_cleared, false, message);
    return result;
}

Result clearAllNvs() {
    Result result{};
    ESP_LOGW(TAG, "Clearing entire NVS partition; reboot required");
    esp_err_t err = nvs_flash_deinit_partition("nvs");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED) {
        char message[160] = {};
        std::snprintf(message, sizeof(message), "nvs deinit failed: %s", esp_err_to_name(err));
        setResult(result, "clear-all-nvs", false, false, message);
        return result;
    }

    err = nvs_flash_erase_partition("nvs");
    if (err != ESP_OK) {
        char message[160] = {};
        std::snprintf(message, sizeof(message), "nvs erase failed: %s", esp_err_to_name(err));
        setResult(result, "clear-all-nvs", false, false, message);
        return result;
    }

    setResult(result, "clear-all-nvs", true, true, "Entire NVS partition erased. Device will reboot.");
    rebootSoon();
    return result;
}

void rebootSoon() {
    xTaskCreate(rebootTask, "maintenance_reboot", 2048, nullptr, 5, nullptr);
}

}  // namespace platform_maintenance
