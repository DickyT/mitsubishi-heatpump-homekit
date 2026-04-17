#include "platform_log.h"

#include "app_config.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"

#include <cstdarg>
#include <cstdio>

namespace {

const char* TAG = "platform_log";

constexpr size_t kMsgBufBytes = 4096;
constexpr size_t kLineBufBytes = 256;
constexpr size_t kFlushIntervalMs = 30000;
constexpr size_t kTaskStackBytes = 3072;

vprintf_like_t previous_vprintf = nullptr;
FILE* log_file = nullptr;
MessageBufferHandle_t msg_buf = nullptr;

int teeVprintf(const char* format, va_list args) {
    va_list console_args;
    va_copy(console_args, args);
    const int written = previous_vprintf ? previous_vprintf(format, console_args) : vprintf(format, console_args);
    va_end(console_args);

    if (msg_buf) {
        char line[kLineBufBytes];
        va_list file_args;
        va_copy(file_args, args);
        const int n = vsnprintf(line, sizeof(line), format, file_args);
        va_end(file_args);

        if (n > 0) {
            const size_t len = static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1;
            xMessageBufferSend(msg_buf, line, len, 0);
        }
    }

    return written;
}

void logWriterTask(void*) {
    char buf[kLineBufBytes];
    TickType_t last_flush = xTaskGetTickCount();

    while (true) {
        const size_t received = xMessageBufferReceive(msg_buf, buf, sizeof(buf) - 1, pdMS_TO_TICKS(kFlushIntervalMs));
        if (received > 0 && log_file) {
            buf[received] = '\0';
            fputs(buf, log_file);
        }

        const TickType_t now = xTaskGetTickCount();
        if (log_file && (now - last_flush) >= pdMS_TO_TICKS(kFlushIntervalMs)) {
            fflush(log_file);
            last_flush = now;
        }
    }
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

    msg_buf = xMessageBufferCreate(kMsgBufBytes);
    if (!msg_buf) {
        ESP_LOGE(TAG, "Failed to create log message buffer");
        fclose(log_file);
        log_file = nullptr;
        return;
    }

    xTaskCreate(logWriterTask, "log_writer", kTaskStackBytes, nullptr, 1, nullptr);
    previous_vprintf = esp_log_set_vprintf(teeVprintf);
    ESP_LOGI(TAG, "Persistent log enabled (async): %s", app_config::kPersistentLogPath);
}

void logStartupSummary() {
    ESP_LOGI(TAG, "Device: %s", app_config::kDeviceName);
    ESP_LOGI(TAG, "Migration phase: %s", app_config::kPhaseName);
    ESP_LOGI(TAG, "Default log level: %s", logLevelName(app_config::kDefaultLogLevel));
}

}  // namespace platform_log
