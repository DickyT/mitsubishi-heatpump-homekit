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

#include "platform_fs.h"

#include "app_config.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

const char* TAG = "platform_fs";
bool mounted = false;

void setMessage(char* message, size_t message_len, const char* value) {
    if (message != nullptr && message_len > 0) {
        std::snprintf(message, message_len, "%s", value == nullptr ? "" : value);
    }
}

bool appendJsonItem(std::string& json, const char* name, const char* type, size_t size) {
    if (!json.empty()) {
        json += ",";
    }
    json += "{\"name\":\"";
    json += platform_fs::jsonEscape(name);
    json += "\",\"type\":\"";
    json += type;
    json += "\",\"size\":";
    json += std::to_string(size);
    json += "}";
    return true;
}

std::string logicalNameFromDirent(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return "/";
    }

    std::string value(name);
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

bool normalizePath(const char* raw_path, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return false;
    }

    out[0] = '\0';
    const char* src = raw_path == nullptr ? "/" : raw_path;
    while (std::isspace(static_cast<unsigned char>(*src))) {
        ++src;
    }

    size_t written = 0;
    bool previous_slash = false;
    if (*src != '/') {
        out[written++] = '/';
        previous_slash = true;
    }

    size_t i = 0;
    for (; src[i] != '\0' && written + 1 < out_len; ++i) {
        char c = src[i] == '\\' ? '/' : src[i];
        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }
        if (c == '/') {
            if (previous_slash) {
                continue;
            }
            previous_slash = true;
        } else {
            previous_slash = false;
        }
        out[written++] = c;
    }

    while (std::isspace(static_cast<unsigned char>(src[i]))) {
        ++i;
    }
    if (src[i] != '\0') {
        out[0] = '\0';
        return false;
    }

    while (written > 1 && std::isspace(static_cast<unsigned char>(out[written - 1]))) {
        --written;
    }
    while (written > 1 && out[written - 1] == '/') {
        --written;
    }
    out[written] = '\0';
    return written > 0;
}

bool isSafePath(const char* raw_path) {
    char path[160] = {};
    if (!normalizePath(raw_path, path, sizeof(path))) {
        return false;
    }
    return std::strcmp(path, "/.keep") != 0 &&
           std::strstr(path, "..") == nullptr &&
           std::strstr(path, "//") == nullptr;
}

bool toPhysicalPath(const char* raw_path, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0 || !isSafePath(raw_path)) {
        return false;
    }

    char logical[160] = {};
    if (!normalizePath(raw_path, logical, sizeof(logical))) {
        return false;
    }

    const int written = std::strcmp(logical, "/") == 0
        ? std::snprintf(out, out_len, "%s", app_config::kSpiffsBasePath)
        : std::snprintf(out, out_len, "%s%s", app_config::kSpiffsBasePath, logical);
    return written > 0 && static_cast<size_t>(written) < out_len;
}

bool exists(const char* raw_path) {
    char physical[192] = {};
    if (!mounted || !toPhysicalPath(raw_path, physical, sizeof(physical))) {
        return false;
    }
    struct stat st {};
    return stat(physical, &st) == 0;
}

size_t fileSize(const char* raw_path) {
    char physical[192] = {};
    if (!mounted || !toPhysicalPath(raw_path, physical, sizeof(physical))) {
        return 0;
    }
    struct stat st {};
    if (stat(physical, &st) != 0) {
        return 0;
    }
    return static_cast<size_t>(st.st_size);
}

FILE* openRead(const char* raw_path) {
    char physical[192] = {};
    if (!mounted || !toPhysicalPath(raw_path, physical, sizeof(physical))) {
        return nullptr;
    }
    return std::fopen(physical, "r");
}

FILE* openWrite(const char* raw_path, const char* mode) {
    char physical[192] = {};
    if (!mounted || !toPhysicalPath(raw_path, physical, sizeof(physical))) {
        return nullptr;
    }
    return std::fopen(physical, mode == nullptr ? "w" : mode);
}

bool removePath(const char* raw_path, char* message, size_t message_len) {
    if (!mounted) {
        setMessage(message, message_len, "SPIFFS is not mounted");
        return false;
    }

    char path[160] = {};
    if (!normalizePath(raw_path, path, sizeof(path)) || !isSafePath(path) || std::strcmp(path, "/") == 0) {
        setMessage(message, message_len, "invalid path");
        return false;
    }

    char physical[192] = {};
    if (!toPhysicalPath(path, physical, sizeof(physical))) {
        setMessage(message, message_len, "invalid path");
        return false;
    }

    if (unlink(physical) == 0) {
        setMessage(message, message_len, "file deleted");
        return true;
    }

    std::string prefix = std::string(path) + "/";
    std::string keep = prefix + ".keep";
    int child_count = 0;
    DIR* root = opendir(app_config::kSpiffsBasePath);
    if (root != nullptr) {
        while (dirent* entry = readdir(root)) {
            const std::string logical = logicalNameFromDirent(entry->d_name);
            if (logical.rfind(prefix, 0) == 0) {
                child_count++;
                if (logical != keep) {
                    closedir(root);
                    setMessage(message, message_len, "directory is not empty");
                    return false;
                }
            }
        }
        closedir(root);
    }

    if (child_count == 1) {
        char keep_physical[192] = {};
        if (toPhysicalPath(keep.c_str(), keep_physical, sizeof(keep_physical)) && unlink(keep_physical) == 0) {
            setMessage(message, message_len, "directory deleted");
            return true;
        }
    }

    setMessage(message, message_len, "path not found");
    return false;
}

bool createFile(const char* raw_path, const char* content, size_t content_len, char* message, size_t message_len) {
    if (!mounted) {
        setMessage(message, message_len, "SPIFFS is not mounted");
        return false;
    }

    char path[160] = {};
    if (!normalizePath(raw_path, path, sizeof(path)) || !isSafePath(path) || std::strcmp(path, "/") == 0) {
        setMessage(message, message_len, "invalid path");
        return false;
    }

    FILE* file = openWrite(path, "w");
    if (file == nullptr) {
        setMessage(message, message_len, "file open failed");
        return false;
    }

    const size_t written = content_len > 0 && content != nullptr ? std::fwrite(content, 1, content_len, file) : 0;
    std::fclose(file);
    if (written < content_len) {
        setMessage(message, message_len, "partial write");
        return false;
    }

    setMessage(message, message_len, "file saved");
    return true;
}

bool createDirectory(const char* raw_path, char* message, size_t message_len) {
    char dir[160] = {};
    if (!normalizePath(raw_path, dir, sizeof(dir)) || !isSafePath(dir) || std::strcmp(dir, "/") == 0) {
        setMessage(message, message_len, "invalid directory path");
        return false;
    }

    std::string keep = std::string(dir) + "/.keep";
    const bool ok = createFile(keep.c_str(), "", 0, message, message_len);
    if (ok) {
        setMessage(message, message_len, "directory created");
    }
    return ok;
}

bool removeAllFilesExcept(const char* protected_path, char* message, size_t message_len) {
    if (!mounted) {
        setMessage(message, message_len, "SPIFFS is not mounted");
        return false;
    }

    char protected_normalized[160] = {};
    if (protected_path != nullptr && protected_path[0] != '\0') {
        normalizePath(protected_path, protected_normalized, sizeof(protected_normalized));
    }

    DIR* root = opendir(app_config::kSpiffsBasePath);
    if (root == nullptr) {
        setMessage(message, message_len, "open directory failed");
        return false;
    }

    size_t removed = 0;
    size_t failed = 0;
    while (dirent* entry = readdir(root)) {
        const std::string logical = logicalNameFromDirent(entry->d_name);
        if (logical == "/" || (!logical.empty() && logical == protected_normalized)) {
            continue;
        }

        char physical[192] = {};
        if (toPhysicalPath(logical.c_str(), physical, sizeof(physical)) && unlink(physical) == 0) {
            removed++;
        } else {
            failed++;
        }
    }
    closedir(root);

    char out[160] = {};
    std::snprintf(out,
                  sizeof(out),
                  "removed=%u failed=%u active_log_kept=%s",
                  static_cast<unsigned>(removed),
                  static_cast<unsigned>(failed),
                  protected_normalized[0] == '\0' ? "false" : "true");
    setMessage(message, message_len, out);
    return failed == 0;
}

std::string joinPath(const char* raw_dir, const char* raw_name) {
    char dir[160] = {};
    normalizePath(raw_dir, dir, sizeof(dir));

    const char* name = raw_name == nullptr ? "" : raw_name;
    const char* slash = std::strrchr(name, '/');
    if (slash != nullptr) {
        name = slash + 1;
    }
    const char* backslash = std::strrchr(name, '\\');
    if (backslash != nullptr) {
        name = backslash + 1;
    }

    std::string joined = std::strcmp(dir, "/") == 0 ? "/" : std::string(dir) + "/";
    joined += name;
    char normalized[160] = {};
    if (!normalizePath(joined.c_str(), normalized, sizeof(normalized))) {
        return "/";
    }
    return normalized;
}

std::string listJson(const char* raw_dir) {
    if (!mounted) {
        return "{\"ok\":false,\"error\":\"SPIFFS is not mounted\"}";
    }

    char dir[160] = {};
    if (!normalizePath(raw_dir, dir, sizeof(dir)) || !isSafePath(dir)) {
        return "{\"ok\":false,\"error\":\"invalid directory\"}";
    }

    std::string prefix = std::strcmp(dir, "/") == 0 ? "/" : std::string(dir) + "/";
    std::string items;
    std::vector<std::string> seen_dirs;
    size_t total_items = 0;
    size_t returned_items = 0;

    const auto seenDir = [&seen_dirs](const std::string& name) {
        return std::find(seen_dirs.begin(), seen_dirs.end(), name) != seen_dirs.end();
    };
    const auto appendLimited = [&](const char* name, const char* type, size_t size) {
        total_items++;
        if (returned_items >= app_config::kWebFileListMaxItems) {
            return;
        }
        appendJsonItem(items, name, type, size);
        returned_items++;
    };

    DIR* root = opendir(app_config::kSpiffsBasePath);
    if (root == nullptr) {
        return "{\"ok\":false,\"error\":\"open directory failed\"}";
    }

    while (dirent* entry = readdir(root)) {
        const std::string logical = logicalNameFromDirent(entry->d_name);
        if (logical == "/") {
            continue;
        }
        if (logical.rfind(prefix, 0) != 0 || logical == prefix + ".keep") {
            continue;
        }

        const std::string rest = logical.substr(prefix.length());
        const size_t slash = rest.find('/');
        if (slash != std::string::npos) {
            const std::string child_dir = prefix + rest.substr(0, slash);
            if (!seenDir(child_dir)) {
                seen_dirs.push_back(child_dir);
                appendLimited(child_dir.c_str(), "dir", 0);
            }
        } else {
            size_t size = 0;
            if (returned_items < app_config::kWebFileListMaxItems) {
                char physical[192] = {};
                struct stat st {};
                if (toPhysicalPath(logical.c_str(), physical, sizeof(physical)) && stat(physical, &st) == 0) {
                    size = static_cast<size_t>(st.st_size);
                }
            }
            appendLimited(logical.c_str(), "file", size);
        }
    }
    closedir(root);

    std::string json = "{\"ok\":true,\"dir\":\"";
    json += jsonEscape(dir);
    json += "\",\"items\":[";
    json += items;
    json += "],\"truncated\":";
    json += total_items > returned_items ? "true" : "false";
    json += ",\"totalItems\":";
    json += std::to_string(total_items);
    json += ",\"returnedItems\":";
    json += std::to_string(returned_items);
    json += ",\"limit\":";
    json += std::to_string(app_config::kWebFileListMaxItems);
    json += ",\"info\":";
    json += infoJson();
    json += "}";
    return json;
}

std::string infoJson() {
    const Status status = getStatus();
    std::string json = "{\"mounted\":";
    json += status.mounted ? "true" : "false";
    json += ",\"totalBytes\":";
    json += std::to_string(status.totalBytes);
    json += ",\"usedBytes\":";
    json += std::to_string(status.usedBytes);
    json += ",\"freeBytes\":";
    json += std::to_string(status.freeBytes);
    json += "}";
    return json;
}

std::string jsonEscape(const char* input) {
    std::string out;
    if (input == nullptr) {
        return out;
    }
    for (size_t i = 0; input[i] != '\0'; ++i) {
        const char c = input[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace platform_fs
