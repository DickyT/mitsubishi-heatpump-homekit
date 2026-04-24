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

#include <cstddef>
#include <cstdio>
#include <string>

namespace platform_log {

struct Status {
    bool active = false;
    char currentPath[128] = "";
    size_t currentBytes = 0;
    size_t droppedLines = 0;
    char levelName[16] = "";
};

void init();
void applyConfiguredLogLevel();
void enablePersistentLog();
void logStartupSummary();
Status getStatus();
bool openLogFile(const char* requested_path, FILE** out);
bool readLiveLog(size_t offset, size_t max_bytes, char* out, size_t out_len, size_t* next_offset, size_t* file_size, bool* reset);
bool clearCurrentLog(const char* reason);
bool clearAllLogs(char* message, size_t message_len);
std::string logsJson();

}  // namespace platform_log
