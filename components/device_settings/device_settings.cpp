#include "device_settings.h"

#include "app_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <cstdio>
#include <cstring>

namespace {

const char* TAG = "device_settings";
const char* kNamespace = "device_cfg";

device_settings::Settings settings{};
bool initialized = false;
char setup_code_canonical[12] = "";
char setup_code_display[10] = "";

const char* logLevelNameLocal(esp_log_level_t level) {
    switch (level) {
        case ESP_LOG_ERROR: return "error";
        case ESP_LOG_WARN: return "warn";
        case ESP_LOG_INFO: return "info";
        case ESP_LOG_DEBUG: return "debug";
        case ESP_LOG_VERBOSE: return "verbose";
        default: return "unknown";
    }
}

void copyString(char* out, size_t out_len, const char* value) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    std::snprintf(out, out_len, "%s", value == nullptr ? "" : value);
}

void setMessage(char* out, size_t out_len, const char* value) {
    copyString(out, out_len, value);
}

void loadDefaults() {
    copyString(settings.deviceName, sizeof(settings.deviceName), app_config::kHomeKitAccessoryName);
    settings.homeKitCode[0] = '\0';
    settings.useRealCn105 = app_config::kCn105UseRealTransport;
    settings.statusLedEnabled = app_config::kStatusLedEnabled;
    settings.cn105BaudRate = app_config::kCn105BaudRate;
    settings.pollIntervalActiveMs = 15000;
    settings.pollIntervalOffMs = 60000;
    settings.logLevel = app_config::kDefaultLogLevel;
}

esp_err_t ensureNvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

bool validBaud(int value) {
    return value == 2400 || value == 4800 || value == 9600;
}

bool sanitizeHomeKitCode(const char* value, char* out_digits, size_t out_len) {
    if (out_digits == nullptr || out_len < 9) {
        return false;
    }
    size_t count = 0;
    if (value != nullptr) {
        for (size_t i = 0; value[i] != '\0'; ++i) {
            if (value[i] >= '0' && value[i] <= '9') {
                if (count >= 8) {
                    return false;
                }
                out_digits[count++] = value[i];
            } else if (value[i] != '-' && value[i] != ' ' && value[i] != '\t') {
                return false;
            }
        }
    }
    if (count != 8) {
        return false;
    }
    out_digits[count] = '\0';
    return true;
}

void refreshCodeFormats() {
    if (std::strlen(settings.homeKitCode) != 8) {
        setup_code_canonical[0] = '\0';
        setup_code_display[0] = '\0';
        return;
    }

    std::snprintf(setup_code_canonical,
                  sizeof(setup_code_canonical),
                  "%c%c%c-%c%c-%c%c%c",
                  settings.homeKitCode[0],
                  settings.homeKitCode[1],
                  settings.homeKitCode[2],
                  settings.homeKitCode[3],
                  settings.homeKitCode[4],
                  settings.homeKitCode[5],
                  settings.homeKitCode[6],
                  settings.homeKitCode[7]);

    std::snprintf(setup_code_display,
                  sizeof(setup_code_display),
                  "%c%c%c%c-%c%c%c%c",
                  settings.homeKitCode[0],
                  settings.homeKitCode[1],
                  settings.homeKitCode[2],
                  settings.homeKitCode[3],
                  settings.homeKitCode[4],
                  settings.homeKitCode[5],
                  settings.homeKitCode[6],
                  settings.homeKitCode[7]);
}

void generateRandomHomeKitCode(char* out_digits, size_t out_len) {
    if (out_digits == nullptr || out_len < 9) {
        return;
    }
    for (size_t i = 0; i < 8; ++i) {
        out_digits[i] = static_cast<char>('0' + (esp_random() % 10));
    }
    out_digits[8] = '\0';
}

bool validLogLevel(uint8_t value) {
    return value <= static_cast<uint8_t>(ESP_LOG_VERBOSE);
}

}  // namespace

namespace device_settings {

esp_err_t init() {
    if (initialized) {
        return ESP_OK;
    }

    loadDefaults();
    esp_err_t err = ensureNvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    bool wrote_defaults = false;

    size_t len = sizeof(settings.deviceName);
    err = nvs_get_str(handle, "device_name", settings.deviceName, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_str(handle, "device_name", settings.deviceName);
        wrote_defaults = true;
    }

    char stored_homekit_code[sizeof(settings.homeKitCode)] = {};
    len = sizeof(stored_homekit_code);
    err = nvs_get_str(handle, "hk_code", stored_homekit_code, &len);
    if (err == ESP_OK && sanitizeHomeKitCode(stored_homekit_code, settings.homeKitCode, sizeof(settings.homeKitCode))) {
        // Loaded existing code.
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        generateRandomHomeKitCode(settings.homeKitCode, sizeof(settings.homeKitCode));
        nvs_set_str(handle, "hk_code", settings.homeKitCode);
        wrote_defaults = true;
    } else {
        ESP_LOGW(TAG, "Invalid HomeKit setup code in NVS; generating a replacement");
        generateRandomHomeKitCode(settings.homeKitCode, sizeof(settings.homeKitCode));
        nvs_set_str(handle, "hk_code", settings.homeKitCode);
        wrote_defaults = true;
    }

    uint8_t use_real = settings.useRealCn105 ? 1 : 0;
    err = nvs_get_u8(handle, "use_real", &use_real);
    if (err == ESP_OK) {
        settings.useRealCn105 = use_real != 0;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_u8(handle, "use_real", use_real);
        wrote_defaults = true;
    }

    uint8_t led_enabled = settings.statusLedEnabled ? 1 : 0;
    err = nvs_get_u8(handle, "led_on", &led_enabled);
    if (err == ESP_OK) {
        settings.statusLedEnabled = led_enabled != 0;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_u8(handle, "led_on", led_enabled);
        wrote_defaults = true;
    }

    int32_t baud = settings.cn105BaudRate;
    err = nvs_get_i32(handle, "baud", &baud);
    if (err == ESP_OK) {
        if (validBaud(baud)) {
            settings.cn105BaudRate = baud;
        } else {
            ESP_LOGW(TAG, "Invalid CN105 baud rate in NVS: %ld; using default %d",
                     static_cast<long>(baud),
                     settings.cn105BaudRate);
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_i32(handle, "baud", settings.cn105BaudRate);
        wrote_defaults = true;
    }

    uint32_t active_ms = settings.pollIntervalActiveMs;
    err = nvs_get_u32(handle, "poll_on", &active_ms);
    if (err == ESP_OK) {
        if (active_ms >= 1000) {
            settings.pollIntervalActiveMs = active_ms;
        } else {
            ESP_LOGW(TAG, "Invalid active poll interval in NVS: %lu; using default %lu",
                     static_cast<unsigned long>(active_ms),
                     static_cast<unsigned long>(settings.pollIntervalActiveMs));
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_u32(handle, "poll_on", settings.pollIntervalActiveMs);
        wrote_defaults = true;
    }

    uint32_t off_ms = settings.pollIntervalOffMs;
    err = nvs_get_u32(handle, "poll_off", &off_ms);
    if (err == ESP_OK) {
        if (off_ms >= 5000) {
            settings.pollIntervalOffMs = off_ms;
        } else {
            ESP_LOGW(TAG, "Invalid off poll interval in NVS: %lu; using default %lu",
                     static_cast<unsigned long>(off_ms),
                     static_cast<unsigned long>(settings.pollIntervalOffMs));
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_u32(handle, "poll_off", settings.pollIntervalOffMs);
        wrote_defaults = true;
    }

    uint8_t level = static_cast<uint8_t>(settings.logLevel);
    err = nvs_get_u8(handle, "log_level", &level);
    if (err == ESP_OK) {
        if (validLogLevel(level)) {
            settings.logLevel = static_cast<esp_log_level_t>(level);
        } else {
            ESP_LOGW(TAG, "Invalid log level in NVS: %u; using default %s",
                     static_cast<unsigned>(level),
                     logLevelNameLocal(settings.logLevel));
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_u8(handle, "log_level", static_cast<uint8_t>(settings.logLevel));
        wrote_defaults = true;
    }

    if (wrote_defaults) {
        nvs_commit(handle);
    }

    nvs_close(handle);
    refreshCodeFormats();
    initialized = true;
    ESP_LOGI(TAG,
             "Loaded settings: name=%s homekit_code=%s transport=%s led=%s baud=%d poll_on=%lu poll_off=%lu log=%s",
             settings.deviceName,
             setup_code_display,
             settings.useRealCn105 ? "real" : "mock",
             settings.statusLedEnabled ? "on" : "off",
             settings.cn105BaudRate,
             static_cast<unsigned long>(settings.pollIntervalActiveMs),
             static_cast<unsigned long>(settings.pollIntervalOffMs),
             logLevelNameLocal(settings.logLevel));
    return ESP_OK;
}

const Settings& get() {
    return settings;
}

const char* deviceName() {
    return settings.deviceName;
}

const char* homeKitCodeDigits() {
    return settings.homeKitCode;
}

const char* homeKitSetupCode() {
    return setup_code_canonical;
}

const char* homeKitDisplayCode() {
    return setup_code_display;
}

bool useRealCn105() {
    return settings.useRealCn105;
}

bool statusLedEnabled() {
    return settings.statusLedEnabled;
}

int cn105BaudRate() {
    return settings.cn105BaudRate;
}

uint32_t pollIntervalActiveMs() {
    return settings.pollIntervalActiveMs;
}

uint32_t pollIntervalOffMs() {
    return settings.pollIntervalOffMs;
}

esp_log_level_t logLevel() {
    return settings.logLevel;
}

const char* logLevelName() {
    return logLevelNameLocal(settings.logLevel);
}

bool parseLogLevel(const char* value, esp_log_level_t* out) {
    if (value == nullptr || out == nullptr) {
        return false;
    }
    if (std::strcmp(value, "error") == 0) {
        *out = ESP_LOG_ERROR;
        return true;
    }
    if (std::strcmp(value, "warn") == 0) {
        *out = ESP_LOG_WARN;
        return true;
    }
    if (std::strcmp(value, "info") == 0) {
        *out = ESP_LOG_INFO;
        return true;
    }
    if (std::strcmp(value, "debug") == 0) {
        *out = ESP_LOG_DEBUG;
        return true;
    }
    if (std::strcmp(value, "verbose") == 0) {
        *out = ESP_LOG_VERBOSE;
        return true;
    }
    return false;
}

bool save(const Settings& requested, bool* reboot_required, char* message, size_t message_len) {
    if (!initialized && init() != ESP_OK) {
        setMessage(message, message_len, "settings init failed");
        return false;
    }

    Settings next = requested;
    if (next.deviceName[0] == '\0') {
        copyString(next.deviceName, sizeof(next.deviceName), app_config::kHomeKitAccessoryName);
    }
    char sanitized_code[sizeof(next.homeKitCode)] = {};
    if (!sanitizeHomeKitCode(next.homeKitCode, sanitized_code, sizeof(sanitized_code))) {
        setMessage(message, message_len, "invalid HomeKit setup code");
        return false;
    }
    copyString(next.homeKitCode, sizeof(next.homeKitCode), sanitized_code);
    if (!validBaud(next.cn105BaudRate)) {
        setMessage(message, message_len, "invalid CN105 baud rate");
        return false;
    }
    if (next.pollIntervalActiveMs < 1000 || next.pollIntervalOffMs < 5000) {
        setMessage(message, message_len, "poll intervals are too small");
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        setMessage(message, message_len, "nvs_open failed");
        return false;
    }

    bool needs_reboot = false;
    needs_reboot = needs_reboot || std::strcmp(settings.deviceName, next.deviceName) != 0;
    needs_reboot = needs_reboot || std::strcmp(settings.homeKitCode, next.homeKitCode) != 0;
    needs_reboot = needs_reboot || settings.useRealCn105 != next.useRealCn105;
    needs_reboot = needs_reboot || settings.cn105BaudRate != next.cn105BaudRate;

    nvs_set_str(handle, "device_name", next.deviceName);
    nvs_set_str(handle, "hk_code", next.homeKitCode);
    nvs_set_u8(handle, "use_real", next.useRealCn105 ? 1 : 0);
    nvs_set_u8(handle, "led_on", next.statusLedEnabled ? 1 : 0);
    nvs_set_i32(handle, "baud", next.cn105BaudRate);
    nvs_set_u32(handle, "poll_on", next.pollIntervalActiveMs);
    nvs_set_u32(handle, "poll_off", next.pollIntervalOffMs);
    nvs_set_u8(handle, "log_level", static_cast<uint8_t>(next.logLevel));
    const esp_err_t commit_err = nvs_commit(handle);
    nvs_close(handle);
    if (commit_err != ESP_OK) {
        setMessage(message, message_len, "settings commit failed");
        return false;
    }

    settings = next;
    refreshCodeFormats();
    if (reboot_required != nullptr) {
        *reboot_required = needs_reboot;
    }
    std::snprintf(message,
                  message_len,
                  "%s",
                  needs_reboot
                      ? "Settings saved. Reboot required for device name, HomeKit metadata, HomeKit setup code, CN105 mode, or baud changes."
                      : "Settings saved.");
    return true;
}

}  // namespace device_settings
