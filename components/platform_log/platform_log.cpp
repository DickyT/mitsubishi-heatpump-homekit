#include "platform_log.h"

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "platform_fs.h"
#include "platform_lock.h"

#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {

const char* TAG = "platform_log";

constexpr size_t kTaskStackBytes = 3072;

vprintf_like_t previous_vprintf = nullptr;
FILE* log_file = nullptr;
MessageBufferHandle_t msg_buf = nullptr;
platform_lock::RecursiveMutex log_lock;
char current_path[128] = "";
size_t current_bytes = 0;
size_t last_prune_bytes = 0;
size_t dropped_lines = 0;
bool file_active = false;

void copyString(char* out, size_t out_len, const char* value) {
    if (out != nullptr && out_len > 0) {
        std::snprintf(out, out_len, "%s", value == nullptr ? "" : value);
    }
}

bool isLogFile(const char* path) {
    if (path == nullptr) {
        return false;
    }
    const size_t len = std::strlen(path);
    return ((std::strncmp(path, "/20", 3) == 0 || std::strncmp(path, "/boot-", 6) == 0) &&
            len > 8 &&
            std::strcmp(path + len - 8, "-log.txt") == 0);
}

std::string logicalNameFromDirent(const char* name) {
    std::string value(name == nullptr ? "" : name);
    const std::string base = app_config::kSpiffsBasePath;
    if (value.rfind(base, 0) == 0) {
        value.erase(0, base.length());
    }
    if (value.empty() || value[0] != '/') {
        value.insert(value.begin(), '/');
    }

    char normalized[160] = {};
    if (!platform_fs::normalizePath(value.c_str(), normalized, sizeof(normalized))) {
        return "/";
    }
    return normalized;
}

size_t fileSize(const char* logical_path) {
    char physical[192] = {};
    if (!platform_fs::toPhysicalPath(logical_path, physical, sizeof(physical))) {
        return 0;
    }
    struct stat st {};
    if (stat(physical, &st) != 0) {
        return 0;
    }
    return static_cast<size_t>(st.st_size);
}

std::string makeTimestampedBaseName() {
    time_t now = 0;
    time(&now);
    struct tm time_info {};
    localtime_r(&now, &time_info);
    if (time_info.tm_year + 1900 >= 2024) {
        char buffer[48] = {};
        strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H-%M-%S-log.txt", &time_info);
        return buffer;
    }

    char fallback[48] = {};
    std::snprintf(fallback,
                  sizeof(fallback),
                  "boot-%llu-log.txt",
                  static_cast<unsigned long long>(esp_timer_get_time() / 1000));
    return fallback;
}

std::string makeUniquePath(const std::string& base_name) {
    std::string path = "/" + base_name;
    if (!platform_fs::exists(path.c_str())) {
        return path;
    }

    const size_t dot = base_name.rfind('.');
    const std::string stem = dot == std::string::npos ? base_name : base_name.substr(0, dot);
    const std::string ext = dot == std::string::npos ? "" : base_name.substr(dot);
    for (int i = 2; i < 100; ++i) {
        path = "/" + stem + "-" + std::to_string(i) + ext;
        if (!platform_fs::exists(path.c_str())) {
            return path;
        }
    }

    char fallback[48] = {};
    std::snprintf(fallback,
                  sizeof(fallback),
                  "/boot-%llu-log.txt",
                  static_cast<unsigned long long>(esp_timer_get_time() / 1000));
    return fallback;
}

size_t totalLogBytes() {
    size_t total = 0;
    DIR* root = opendir(app_config::kSpiffsBasePath);
    if (root == nullptr) {
        return total;
    }

    while (dirent* entry = readdir(root)) {
        const std::string name = logicalNameFromDirent(entry->d_name);
        if (isLogFile(name.c_str())) {
            total += fileSize(name.c_str());
        }
    }
    closedir(root);
    return total;
}

std::string oldestLogPath() {
    std::string oldest;
    DIR* root = opendir(app_config::kSpiffsBasePath);
    if (root == nullptr) {
        return oldest;
    }

    while (dirent* entry = readdir(root)) {
        const std::string name = logicalNameFromDirent(entry->d_name);
        if (isLogFile(name.c_str()) && name != current_path && (oldest.empty() || name < oldest)) {
            oldest = name;
        }
    }
    closedir(root);
    return oldest;
}

void pruneLogFiles() {
    while (totalLogBytes() > app_config::kPersistentLogMaxTotalBytes) {
        const std::string oldest = oldestLogPath();
        if (oldest.empty()) {
            return;
        }

        char physical[192] = {};
        if (platform_fs::toPhysicalPath(oldest.c_str(), physical, sizeof(physical))) {
            unlink(physical);
        } else {
            return;
        }
    }
}

bool hasFreeBytes(size_t len) {
    const platform_fs::Status status = platform_fs::getStatus();
    return status.mounted && status.freeBytes >= len;
}

bool clearCurrentLogForSpace(const char* reason, size_t requested_len) {
    char path[sizeof(current_path)] = {};
    copyString(path, sizeof(path), current_path);
    if (path[0] == '\0') {
        return false;
    }

    if (log_file != nullptr) {
        std::fclose(log_file);
        log_file = nullptr;
    }

    char physical[192] = {};
    if (platform_fs::toPhysicalPath(path, physical, sizeof(physical))) {
        unlink(physical);
    }

    log_file = platform_fs::openWrite(path, "w");
    if (log_file == nullptr) {
        file_active = false;
        current_path[0] = '\0';
        current_bytes = 0;
        last_prune_bytes = 0;
        return false;
    }

    file_active = true;
    current_bytes = 0;
    last_prune_bytes = 0;

    char notice[192] = {};
    std::snprintf(notice,
                  sizeof(notice),
                  "[platform_log] Cleared current log because %s; continuing with new entries\n",
                  reason == nullptr ? "space pressure" : reason);
    const size_t notice_len = std::strlen(notice);
    const size_t written = std::fwrite(notice, 1, notice_len, log_file);
    std::fflush(log_file);
    current_bytes += written;
    return hasFreeBytes(requested_len);
}

bool ensureSpaceFor(size_t len) {
    if (hasFreeBytes(len)) {
        return true;
    }

    if (log_file != nullptr) {
        std::fflush(log_file);
    }
    pruneLogFiles();
    if (hasFreeBytes(len)) {
        return true;
    }

    return clearCurrentLogForSpace("SPIFFS full", len);
}

void appendToFile(const char* text, size_t len) {
    if (text == nullptr || len == 0) {
        return;
    }

    platform_lock::ScopedLock lock(log_lock);
    if (!file_active || log_file == nullptr) {
        return;
    }

    if (!ensureSpaceFor(len)) {
        return;
    }

    const size_t written = std::fwrite(text, 1, len, log_file);
    current_bytes += written;
    if (written < len && clearCurrentLogForSpace("partial SPIFFS write", len)) {
        const size_t retry_written = std::fwrite(text, 1, len, log_file);
        current_bytes += retry_written;
    }

    if (current_bytes - last_prune_bytes >= app_config::kPersistentLogPruneIntervalBytes) {
        last_prune_bytes = current_bytes;
        pruneLogFiles();
    }
}

int teeVprintf(const char* format, va_list args) {
    va_list console_args;
    va_copy(console_args, args);
    const int written = previous_vprintf ? previous_vprintf(format, console_args) : vprintf(format, console_args);
    va_end(console_args);

    if (msg_buf) {
        char line[app_config::kPersistentLogLineBytes];
        va_list file_args;
        va_copy(file_args, args);
        const int n = vsnprintf(line, sizeof(line), format, file_args);
        va_end(file_args);

        if (n > 0) {
            const size_t len = static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1;
            if (xMessageBufferSend(msg_buf, line, len, 0) == 0) {
                dropped_lines++;
            }
        }
    }

    return written;
}

void logWriterTask(void*) {
    char buf[app_config::kPersistentLogLineBytes];
    TickType_t last_flush = xTaskGetTickCount();

    while (true) {
        const size_t received = xMessageBufferReceive(msg_buf, buf, sizeof(buf) - 1, pdMS_TO_TICKS(app_config::kPersistentLogFlushIntervalMs));
        if (received > 0) {
            buf[received] = '\0';
            appendToFile(buf, received);
        }

        const TickType_t now = xTaskGetTickCount();
        if ((now - last_flush) >= pdMS_TO_TICKS(app_config::kPersistentLogFlushIntervalMs)) {
            platform_lock::ScopedLock lock(log_lock);
            if (log_file != nullptr) {
                std::fflush(log_file);
            }
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
    setenv("TZ", app_config::kPersistentLogTimezone, 1);
    tzset();
}

void enablePersistentLog() {
    if (!app_config::kPersistentLogEnabled || log_file) {
        return;
    }

    const std::string path = makeUniquePath(makeTimestampedBaseName());
    platform_lock::ScopedLock lock(log_lock);
    copyString(current_path, sizeof(current_path), path.c_str());
    log_file = platform_fs::openWrite(current_path, "w");
    if (!log_file) {
        ESP_LOGE(TAG, "Unable to open persistent log file: %s", current_path);
        current_path[0] = '\0';
        return;
    }

    msg_buf = xMessageBufferCreate(app_config::kPersistentLogBufferBytes);
    if (!msg_buf) {
        ESP_LOGE(TAG, "Failed to create log message buffer");
        fclose(log_file);
        log_file = nullptr;
        current_path[0] = '\0';
        return;
    }

    file_active = true;
    current_bytes = 0;
    last_prune_bytes = 0;
    const char* start_line = "[platform_log] Persistent log started\n";
    appendToFile(start_line, std::strlen(start_line));
    pruneLogFiles();

    xTaskCreate(logWriterTask, "log_writer", kTaskStackBytes, nullptr, 1, nullptr);
    previous_vprintf = esp_log_set_vprintf(teeVprintf);
    ESP_LOGI(TAG, "Persistent log enabled (async): %s", current_path);
}

void logStartupSummary() {
    ESP_LOGI(TAG, "Device: %s", app_config::kDeviceName);
    ESP_LOGI(TAG, "Migration phase: %s", app_config::kPhaseName);
    ESP_LOGI(TAG, "Default log level: %s", logLevelName(app_config::kDefaultLogLevel));
}

Status getStatus() {
    platform_lock::ScopedLock lock(log_lock);
    Status status{};
    status.active = file_active;
    copyString(status.currentPath, sizeof(status.currentPath), current_path);
    status.currentBytes = current_bytes;
    status.droppedLines = dropped_lines;
    copyString(status.levelName, sizeof(status.levelName), logLevelName(app_config::kDefaultLogLevel));
    return status;
}

bool openLogFile(const char* requested_path, FILE** out) {
    if (out == nullptr) {
        return false;
    }
    *out = nullptr;

    char path[160] = {};
    if (requested_path == nullptr || requested_path[0] == '\0') {
        platform_lock::ScopedLock lock(log_lock);
        copyString(path, sizeof(path), current_path);
    } else if (!platform_fs::normalizePath(requested_path, path, sizeof(path))) {
        return false;
    }

    if (!isLogFile(path) || !platform_fs::isSafePath(path)) {
        return false;
    }

    *out = platform_fs::openRead(path);
    return *out != nullptr;
}

bool readLiveLog(size_t offset, size_t max_bytes, char* out, size_t out_len, size_t* next_offset, size_t* file_size, bool* reset) {
    if (out == nullptr || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    char path[160] = {};
    {
        platform_lock::ScopedLock lock(log_lock);
        if (log_file != nullptr) {
            std::fflush(log_file);
        }
        copyString(path, sizeof(path), current_path);
    }

    FILE* file = nullptr;
    if (!openLogFile(path, &file)) {
        return false;
    }

    const size_t size = fileSize(path);
    bool did_reset = false;
    if (offset == 0 && size > max_bytes) {
        offset = size - max_bytes;
        did_reset = true;
    } else if (offset > size) {
        offset = 0;
        did_reset = true;
    }

    std::fseek(file, static_cast<long>(offset), SEEK_SET);
    const size_t limit = std::min(max_bytes, out_len - 1);
    const size_t read = std::fread(out, 1, limit, file);
    out[read] = '\0';
    const long pos = std::ftell(file);
    std::fclose(file);

    if (next_offset != nullptr) {
        *next_offset = pos >= 0 ? static_cast<size_t>(pos) : offset + read;
    }
    if (file_size != nullptr) {
        *file_size = size;
    }
    if (reset != nullptr) {
        *reset = did_reset;
    }
    return true;
}

bool clearCurrentLog(const char* reason) {
    platform_lock::ScopedLock lock(log_lock);
    return clearCurrentLogForSpace(reason == nullptr ? "manual clear" : reason, 1);
}

bool clearAllLogs(char* message, size_t message_len) {
    char current[sizeof(current_path)] = {};
    {
        platform_lock::ScopedLock lock(log_lock);
        copyString(current, sizeof(current), current_path);
    }

    size_t removed = 0;
    size_t failed = 0;
    DIR* root = opendir(app_config::kSpiffsBasePath);
    if (root == nullptr) {
        if (message != nullptr && message_len > 0) {
            std::snprintf(message, message_len, "open directory failed");
        }
        return false;
    }

    while (dirent* entry = readdir(root)) {
        const std::string name = logicalNameFromDirent(entry->d_name);
        if (!isLogFile(name.c_str()) || name == current) {
            continue;
        }
        char physical[192] = {};
        if (platform_fs::toPhysicalPath(name.c_str(), physical, sizeof(physical)) && unlink(physical) == 0) {
            removed++;
        } else {
            failed++;
        }
    }
    closedir(root);

    const bool current_ok = clearCurrentLog("manual log clear");
    if (message != nullptr && message_len > 0) {
        std::snprintf(message,
                      message_len,
                      "removed_old_logs=%u failed=%u current_log_cleared=%s",
                      static_cast<unsigned>(removed),
                      static_cast<unsigned>(failed),
                      current_ok ? "true" : "false");
    }
    return failed == 0 && current_ok;
}

std::string logsJson() {
    std::string json = "[";
    bool first = true;
    DIR* root = opendir(app_config::kSpiffsBasePath);
    if (root == nullptr) {
        json += "]";
        return json;
    }

    char current[sizeof(current_path)] = {};
    {
        platform_lock::ScopedLock lock(log_lock);
        copyString(current, sizeof(current), current_path);
    }

    while (dirent* entry = readdir(root)) {
        const std::string name = logicalNameFromDirent(entry->d_name);
        if (!isLogFile(name.c_str())) {
            continue;
        }
        if (!first) {
            json += ",";
        }
        json += "{\"name\":\"";
        json += platform_fs::jsonEscape(name.c_str());
        json += "\",\"size\":";
        json += std::to_string(fileSize(name.c_str()));
        json += ",\"current\":";
        json += name == current ? "true" : "false";
        json += "}";
        first = false;
    }
    closedir(root);
    json += "]";
    return json;
}

}  // namespace platform_log
