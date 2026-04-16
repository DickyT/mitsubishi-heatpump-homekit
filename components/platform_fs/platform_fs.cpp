#include "platform_fs.h"

#include "app_config.h"
#include "esp_log.h"
#include "esp_spiffs.h"

namespace {

const char* TAG = "platform_fs";
bool mounted = false;

}  // namespace

namespace platform_fs {

esp_err_t init() {
    const esp_vfs_spiffs_conf_t config = {
        .base_path = app_config::kSpiffsBasePath,
        .partition_label = app_config::kSpiffsPartitionLabel,
        .max_files = app_config::kSpiffsMaxOpenFiles,
        .format_if_mount_failed = true,
    };

    ESP_LOGI(TAG, "Mounting SPIFFS at %s", app_config::kSpiffsBasePath);
    const esp_err_t err = esp_vfs_spiffs_register(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    mounted = true;
    ESP_LOGI(TAG, "SPIFFS mounted");
    logStats();
    return ESP_OK;
}

const char* basePath() {
    return app_config::kSpiffsBasePath;
}

Status getStatus() {
    Status status{};
    status.mounted = mounted;

    const esp_err_t err = esp_spiffs_info(app_config::kSpiffsPartitionLabel, &status.totalBytes, &status.usedBytes);
    if (err != ESP_OK) {
        return status;
    }

    status.freeBytes = status.totalBytes >= status.usedBytes ? status.totalBytes - status.usedBytes : 0;
    return status;
}

void logStats() {
    const Status status = getStatus();
    if (!status.mounted || status.totalBytes == 0) {
        ESP_LOGW(TAG, "SPIFFS stats unavailable");
        return;
    }

    ESP_LOGI(TAG, "SPIFFS stats: used=%u total=%u free=%u",
             static_cast<unsigned>(status.usedBytes),
             static_cast<unsigned>(status.totalBytes),
             static_cast<unsigned>(status.freeBytes));
}

}  // namespace platform_fs
