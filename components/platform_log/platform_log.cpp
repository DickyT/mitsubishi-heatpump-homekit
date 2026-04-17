#include "platform_log.h"

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <cstdarg>
#include <cstdio>

namespace {

const char* TAG = "platform_log";

vprintf_like_t previous_vprintf = nullptr;
FILE* log_file = nullptr;
int64_t last_flush_us = 0;
constexpr int64_t kFlushIntervalUs = 30 * 1000 * 1000;

int teeVprintf(const char* format, va_list args) {
    va_list console_args;
    va_copy(console_args, args);
    const int written = previous_vprintf ? previous_vprintf(format, console_args) : vprintf(format, console_args);
    va_end(console_args);

    if (log_file) {
        va_list file_args;
        va_copy(file_args, args);
        vfprintf(log_file, format, file_args);
        va_end(file_args);

        const int64_t now = esp_timer_get_time();
        if (now - last_flush_us >= kFlushIntervalUs) {
            fflush(log_file);
            last_flush_us = now;
        }
    }

    return written;
}

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

void enablePersistentLog() {
    if (log_file) {
        return;
    }

    log_file = fopen(app_config::kPersistentLogPath, "w");
    if (!log_file) {
        ESP_LOGE(TAG, "Unable to open persistent log file: %s", app_config::kPersistentLogPath);
        return;
    }

    previous_vprintf = esp_log_set_vprintf(teeVprintf);
    ESP_LOGI(TAG, "Persistent log enabled: %s", app_config::kPersistentLogPath);
}

void logStartupSummary() {
    ESP_LOGI(TAG, "Device: %s", app_config::kDeviceName);
    ESP_LOGI(TAG, "Migration phase: %s", app_config::kPhaseName);
    ESP_LOGI(TAG, "Default log level: %s", logLevelName(app_config::kDefaultLogLevel));
}

}  // namespace platform_log
