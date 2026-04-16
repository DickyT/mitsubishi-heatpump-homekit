#include "platform_log.h"

#include "app_config.h"
#include "esp_log.h"

namespace {

const char* TAG = "platform_log";

const char* logLevelName(esp_log_level_t level) {
    switch (level) {
        case ESP_LOG_NONE:
            return "none";
        case ESP_LOG_ERROR:
            return "error";
        case ESP_LOG_WARN:
            return "warn";
        case ESP_LOG_INFO:
            return "info";
        case ESP_LOG_DEBUG:
            return "debug";
        case ESP_LOG_VERBOSE:
            return "verbose";
        default:
            return "unknown";
    }
}

}  // namespace

namespace platform_log {

void init() {
    esp_log_level_set("*", app_config::kDefaultLogLevel);
}

void logStartupSummary() {
    ESP_LOGI(TAG, "Device: %s", app_config::kDeviceName);
    ESP_LOGI(TAG, "Migration phase: %s", app_config::kPhaseName);
    ESP_LOGI(TAG, "Default log level: %s", logLevelName(app_config::kDefaultLogLevel));
}

}  // namespace platform_log
