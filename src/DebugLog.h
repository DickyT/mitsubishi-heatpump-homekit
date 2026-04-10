#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "AppConfig.h"
#include "FileSystemManager.h"

class DebugLog {
public:
    static void begin(uint32_t baud) {
        if (AppConfig::LOG_TO_SERIAL) {
            Serial.begin(baud);
        }
        earlyBuffer().reserve(AppConfig::LOG_EARLY_BUFFER_BYTES);
    }

    static void beginPersistentLog() {
        if (!AppConfig::LOG_TO_FILE || fileInitialized()) {
            return;
        }

        fileInitialized() = true;
        if (!FileSystemManager::begin(true)) {
            if (AppConfig::LOG_TO_SERIAL) {
                Serial.println("[DebugLog] SPIFFS mount failed, persistent logging disabled");
            }
            return;
        }

        fsReady() = true;
        String path = makeUniquePath(makeTimestampedBaseName());
        File file = SPIFFS.open(path, FILE_WRITE);
        if (!file) {
            if (AppConfig::LOG_TO_SERIAL) {
                Serial.printf("[DebugLog] Failed to open %s, persistent logging disabled\n", path.c_str());
            }
            return;
        }

        logFile() = file;
        logPath() = path;
        logBytes() = 0;
        lastPruneBytes() = 0;
        fileActive() = true;

        appendFileLine(String("[DebugLog] Persistent log started: ") + path);
        if (earlyTruncated()) {
            appendFileLine("[DebugLog] Early boot log buffer was truncated");
        }
        if (earlyBuffer().length() > 0) {
            appendToFile(earlyBuffer().c_str(), earlyBuffer().length());
            earlyBuffer() = "";
        }

        pruneLogFiles();
    }

    static void printf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vlogf(AppConfig::LogLevel::Info, format, args);
        va_end(args);
    }

    static void printf(AppConfig::LogLevel level, const char* format, ...) {
        va_list args;
        va_start(args, format);
        vlogf(level, format, args);
        va_end(args);
    }

    static void debugf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vlogf(AppConfig::LogLevel::Debug, format, args);
        va_end(args);
    }

    static void warnf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vlogf(AppConfig::LogLevel::Warn, format, args);
        va_end(args);
    }

    static void println() {
        writeText(AppConfig::LogLevel::Info, "\n", 1);
    }

    static void println(const char* value) {
        print(value);
        println();
    }

    static void println(const String& value) {
        print(value);
        println();
    }

    static void print(const char* value) {
        if (!value) {
            return;
        }
        writeText(AppConfig::LogLevel::Info, value, strlen(value));
    }

    static void print(const String& value) {
        writeText(AppConfig::LogLevel::Info, value.c_str(), value.length());
    }

    static String currentLogPath() {
        return logPath();
    }

    static bool persistentLogActive() {
        return fileActive();
    }

    static size_t currentLogSize() {
        if (fileActive()) {
            return logBytes();
        }
        if (logPath().length() == 0 || !fsReady()) {
            return 0;
        }
        File file = SPIFFS.open(logPath(), FILE_READ);
        if (!file) {
            return 0;
        }
        size_t size = file.size();
        file.close();
        return size;
    }

    static String logsJson() {
        if (!fsReady()) {
            return "[]";
        }

        String json = "[";
        bool first = true;
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file) {
            String name = FileSystemManager::normalizePath(file.name());
            if (isLogFile(name)) {
                if (!first) {
                    json += ",";
                }
                json += "{";
                json += "\"name\":\"" + jsonEscape(name) + "\",";
                json += "\"size\":" + String(file.size()) + ",";
                json += "\"current\":" + String(name == logPath() ? "true" : "false");
                json += "}";
                first = false;
            }
            file = root.openNextFile();
        }
        json += "]";
        return json;
    }

    static File openLogFile(const String& requestedName) {
        if (!fsReady()) {
            return File();
        }

        String path;
        if (!normalizeRequestedLogPath(requestedName, path)) {
            return File();
        }
        return SPIFFS.open(path, FILE_READ);
    }

    static String formatElapsedTime(uint32_t ms) {
        uint32_t totalSeconds = ms / 1000;
        uint32_t days = totalSeconds / 86400;
        uint32_t hours = (totalSeconds % 86400) / 3600;
        uint32_t minutes = (totalSeconds % 3600) / 60;
        uint32_t seconds = totalSeconds % 60;

        char buffer[24];
        snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu",
                 static_cast<unsigned long>(days),
                 static_cast<unsigned long>(hours),
                 static_cast<unsigned long>(minutes),
                 static_cast<unsigned long>(seconds));
        return String(buffer);
    }

    static const char* wifiStatusLabel(wl_status_t status) {
        switch (status) {
            case WL_IDLE_STATUS: return "IDLE";
            case WL_NO_SSID_AVAIL: return "NO_SSID";
            case WL_SCAN_COMPLETED: return "SCAN_DONE";
            case WL_CONNECTED: return "CONNECTED";
            case WL_CONNECT_FAILED: return "CONNECT_FAILED";
            case WL_CONNECTION_LOST: return "CONNECTION_LOST";
            case WL_DISCONNECTED: return "DISCONNECTED";
            default: return "UNKNOWN";
        }
    }

    static const char* wifiModeLabel(wifi_mode_t mode) {
        switch (mode) {
            case WIFI_OFF: return "OFF";
            case WIFI_STA: return "STA";
            case WIFI_AP: return "AP";
            case WIFI_AP_STA: return "AP+STA";
            default: return "UNKNOWN";
        }
    }

private:
    static bool& fsReady() {
        static bool value = false;
        return value;
    }

    static bool& fileInitialized() {
        static bool value = false;
        return value;
    }

    static bool& fileActive() {
        static bool value = false;
        return value;
    }

    static bool& earlyTruncated() {
        static bool value = false;
        return value;
    }

    static size_t& logBytes() {
        static size_t value = 0;
        return value;
    }

    static size_t& lastPruneBytes() {
        static size_t value = 0;
        return value;
    }

    static String& logPath() {
        static String value;
        return value;
    }

    static String& earlyBuffer() {
        static String value;
        return value;
    }

    static File& logFile() {
        static File value;
        return value;
    }

    static bool shouldLog(AppConfig::LogLevel level) {
        if (AppConfig::LOG_LEVEL == AppConfig::LogLevel::Off) {
            return false;
        }
        return static_cast<uint8_t>(level) <= static_cast<uint8_t>(AppConfig::LOG_LEVEL);
    }

    static void vlogf(AppConfig::LogLevel level, const char* format, va_list args) {
        if (!format || !shouldLog(level)) {
            return;
        }

        char stackBuffer[512];
        va_list copy;
        va_copy(copy, args);
        int needed = vsnprintf(stackBuffer, sizeof(stackBuffer), format, copy);
        va_end(copy);

        if (needed < 0) {
            return;
        }

        if (needed < static_cast<int>(sizeof(stackBuffer))) {
            writeText(level, stackBuffer, static_cast<size_t>(needed));
            return;
        }

        char* heapBuffer = static_cast<char*>(malloc(static_cast<size_t>(needed) + 1));
        if (!heapBuffer) {
            writeText(level, stackBuffer, sizeof(stackBuffer) - 1);
            return;
        }

        vsnprintf(heapBuffer, static_cast<size_t>(needed) + 1, format, args);
        writeText(level, heapBuffer, static_cast<size_t>(needed));
        free(heapBuffer);
    }

    static void writeText(AppConfig::LogLevel level, const char* text, size_t len) {
        if (!text || len == 0 || !shouldLog(level)) {
            return;
        }

        if (AppConfig::LOG_TO_SERIAL) {
            Serial.write(reinterpret_cast<const uint8_t*>(text), len);
        }

        if (!AppConfig::LOG_TO_FILE) {
            return;
        }

        if (fileActive()) {
            appendToFile(text, len);
            return;
        }

        appendEarlyBuffer(text, len);
    }

    static void appendEarlyBuffer(const char* text, size_t len) {
        if (fileInitialized() || earlyTruncated()) {
            return;
        }

        if (earlyBuffer().length() + len > AppConfig::LOG_EARLY_BUFFER_BYTES) {
            size_t remaining = AppConfig::LOG_EARLY_BUFFER_BYTES - earlyBuffer().length();
            if (remaining > 0) {
                earlyBuffer() += boundedString(text, remaining);
            }
            earlyTruncated() = true;
            return;
        }

        earlyBuffer() += boundedString(text, len);
    }

    static void appendFileLine(const String& line) {
        appendToFile(line.c_str(), line.length());
        appendToFile("\n", 1);
    }

    static void appendToFile(const char* text, size_t len) {
        if (!fileActive() || !logFile()) {
            return;
        }

        if (!ensureSpaceFor(len)) {
            return;
        }

        size_t written = logFile().write(reinterpret_cast<const uint8_t*>(text), len);
        logFile().flush();
        logBytes() += written;
        if (written < len) {
            if (clearCurrentLogForSpace("partial SPIFFS write", len)) {
                written = logFile().write(reinterpret_cast<const uint8_t*>(text), len);
                logFile().flush();
                logBytes() += written;
            }
        }
        if (logBytes() - lastPruneBytes() >= 4096) {
            lastPruneBytes() = logBytes();
            pruneLogFiles();
        }
    }

    static bool ensureSpaceFor(size_t len) {
        if (hasFreeBytes(len)) {
            return true;
        }

        pruneLogFiles();
        if (hasFreeBytes(len)) {
            return true;
        }

        return clearCurrentLogForSpace("SPIFFS full", len);
    }

    static bool hasFreeBytes(size_t len) {
        size_t total = SPIFFS.totalBytes();
        size_t used = SPIFFS.usedBytes();
        if (used > total) {
            return false;
        }
        return (total - used) >= len;
    }

    static bool clearCurrentLogForSpace(const char* reason, size_t requestedLen = 1) {
        String path = logPath();
        if (path.length() == 0) {
            return false;
        }

        if (logFile()) {
            logFile().close();
        }

        SPIFFS.remove(path);
        File file = SPIFFS.open(path, FILE_WRITE);
        if (!file) {
            fileActive() = false;
            logPath() = "";
            logBytes() = 0;
            lastPruneBytes() = 0;
            if (AppConfig::LOG_TO_SERIAL) {
                Serial.printf("[DebugLog] Failed to reopen %s after clearing current log\n", path.c_str());
            }
            return false;
        }

        logFile() = file;
        logBytes() = 0;
        lastPruneBytes() = 0;
        fileActive() = true;

        const char* label = reason ? reason : "space pressure";
        String notice = String("[DebugLog] Cleared current log because ") + label + "; continuing with new entries\n";
        size_t written = logFile().write(reinterpret_cast<const uint8_t*>(notice.c_str()), notice.length());
        logFile().flush();
        logBytes() += written;

        if (AppConfig::LOG_TO_SERIAL) {
            Serial.printf("[DebugLog] Cleared current log %s because %s; continuing file logging\n",
                          path.c_str(),
                          label);
        }

        return hasFreeBytes(requestedLen);
    }

    static String makeTimestampedBaseName() {
        if (WiFi.status() == WL_CONNECTED) {
            configTzTime(AppConfig::LOG_TIMEZONE, AppConfig::LOG_NTP_SERVER_1, AppConfig::LOG_NTP_SERVER_2);

            struct tm timeInfo;
            uint32_t startMs = millis();
            while (millis() - startMs < AppConfig::LOG_TIME_SYNC_TIMEOUT_MS) {
                if (getLocalTime(&timeInfo, 250)) {
                    char buffer[40];
                    strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H-%M-%S-log.txt", &timeInfo);
                    return String(buffer);
                }
                delay(10);
            }
        }

        return "boot-" + String(millis()) + "-log.txt";
    }

    static String makeUniquePath(const String& baseName) {
        String path = "/" + baseName;
        if (!SPIFFS.exists(path)) {
            return path;
        }

        int dot = baseName.lastIndexOf('.');
        String stem = dot >= 0 ? baseName.substring(0, dot) : baseName;
        String ext = dot >= 0 ? baseName.substring(dot) : "";
        for (int i = 2; i < 100; i++) {
            path = "/" + stem + "-" + String(i) + ext;
            if (!SPIFFS.exists(path)) {
                return path;
            }
        }

        return "/boot-" + String(millis()) + "-log.txt";
    }

    static void pruneLogFiles() {
        if (!fsReady()) {
            return;
        }

        while (totalLogBytes() > AppConfig::LOG_MAX_TOTAL_BYTES) {
            String oldest = oldestLogPath();
            if (oldest.length() == 0) {
                return;
            }
            SPIFFS.remove(oldest);
        }
    }

    static size_t totalLogBytes() {
        size_t total = 0;
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file) {
            String name = FileSystemManager::normalizePath(file.name());
            if (isLogFile(name)) {
                total += file.size();
            }
            file = root.openNextFile();
        }
        return total;
    }

    static String oldestLogPath() {
        String oldest;
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file) {
            String name = FileSystemManager::normalizePath(file.name());
            if (isLogFile(name) && name != logPath() && (oldest.length() == 0 || name < oldest)) {
                oldest = name;
            }
            file = root.openNextFile();
        }
        return oldest;
    }

    static bool isLogFile(const String& path) {
        return (path.startsWith("/20") && path.endsWith("-log.txt")) ||
               (path.startsWith("/boot-") && path.endsWith("-log.txt"));
    }

    static bool normalizeRequestedLogPath(const String& requestedName, String& outPath) {
        String path = requestedName;
        path.trim();
        if (path.length() == 0) {
            path = logPath();
        }
        path = FileSystemManager::normalizePath(path);

        if (path.indexOf("..") >= 0 || path.indexOf("//") >= 0 || !isLogFile(path)) {
            return false;
        }

        outPath = path;
        return true;
    }

    static String jsonEscape(const String& input) {
        String out;
        out.reserve(input.length() + 16);
        for (size_t i = 0; i < input.length(); i++) {
            char c = input[i];
            if (c == '\\' || c == '"') {
                out += '\\';
                out += c;
            } else {
                out += c;
            }
        }
        return out;
    }

    static String boundedString(const char* text, size_t len) {
        String out;
        out.reserve(len);
        for (size_t i = 0; i < len; i++) {
            out += text[i];
        }
        return out;
    }
};
