#pragma once

#include "esp_err.h"
#include "esp_log.h"

#include <cstdint>

namespace device_settings {

struct Settings {
    char deviceName[64] = "";
    char homeKitCode[9] = "";
    bool useRealCn105 = true;
    bool statusLedEnabled = true;
    int cn105BaudRate = 2400;
    uint32_t pollIntervalActiveMs = 15000;
    uint32_t pollIntervalOffMs = 60000;
    esp_log_level_t logLevel = ESP_LOG_INFO;
};

esp_err_t init();
const Settings& get();

const char* deviceName();
const char* homeKitCodeDigits();
const char* homeKitSetupCode();
const char* homeKitDisplayCode();
bool useRealCn105();
bool statusLedEnabled();
int cn105BaudRate();
uint32_t pollIntervalActiveMs();
uint32_t pollIntervalOffMs();
esp_log_level_t logLevel();
const char* logLevelName();

bool save(const Settings& requested, bool* reboot_required, char* message, size_t message_len);
bool parseLogLevel(const char* value, esp_log_level_t* out);

}  // namespace device_settings
