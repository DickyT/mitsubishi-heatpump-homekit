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

#pragma once

#include "esp_err.h"

#include <cstddef>
#include <cstdio>
#include <string>

namespace platform_fs {

struct Status {
    bool mounted = false;
    size_t totalBytes = 0;
    size_t usedBytes = 0;
    size_t freeBytes = 0;
};

esp_err_t init();
const char* basePath();
Status getStatus();
void logStats();

bool normalizePath(const char* raw_path, char* out, size_t out_len);
bool isSafePath(const char* raw_path);
bool toPhysicalPath(const char* raw_path, char* out, size_t out_len);

bool exists(const char* raw_path);
size_t fileSize(const char* raw_path);
FILE* openRead(const char* raw_path);
FILE* openWrite(const char* raw_path, const char* mode = "w");
bool removePath(const char* raw_path, char* message, size_t message_len);
bool createFile(const char* raw_path, const char* content, size_t content_len, char* message, size_t message_len);
bool createDirectory(const char* raw_path, char* message, size_t message_len);
bool removeAllFilesExcept(const char* protected_path, char* message, size_t message_len);

std::string joinPath(const char* raw_dir, const char* raw_name);
std::string listJson(const char* raw_dir);
std::string infoJson();
std::string jsonEscape(const char* input);

}  // namespace platform_fs
