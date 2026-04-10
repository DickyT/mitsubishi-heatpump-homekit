#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#if __has_include("AppSecrets.h")
#include "AppSecrets.h"
#endif

namespace AppConfig {

enum class Cn105TransportMode : uint8_t {
    Mock,
    Real
};

enum class LogLevel : uint8_t {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3,
    Verbose = 4,
    Off = 255
};

static constexpr const char* cn105TransportModeLabel(Cn105TransportMode mode) {
    switch (mode) {
        case Cn105TransportMode::Real:
            return "Real";
        case Cn105TransportMode::Mock:
        default:
            return "Mock";
    }
}

static const uint32_t SERIAL_BAUD = 115200;
static const uint16_t WEB_PORT = 8080;
static constexpr Cn105TransportMode CN105_TRANSPORT_MODE = Cn105TransportMode::Mock;
static const uint32_t CN105_UART_BAUD = 2400;
static const int CN105_UART_PORT = 1;
static const int CN105_RX_PIN = 32;
static const int CN105_TX_PIN = 26;
static const uint32_t CN105_RX_TIMEOUT_MS = 120;
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static const bool WIFI_DISABLE_SLEEP = true;
static const bool WIFI_DISABLE_POWER_SAVE = true;
static const uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
static const uint32_t HEARTBEAT_INTERVAL_MS = 5000;
static constexpr LogLevel LOG_LEVEL = LogLevel::Verbose;
static constexpr bool LOG_TO_SERIAL = true;
static constexpr bool LOG_TO_FILE = true;
static constexpr size_t LOG_MAX_TOTAL_BYTES = 192 * 1024;
static constexpr size_t LOG_EARLY_BUFFER_BYTES = 4096;
static constexpr uint32_t LOG_TIME_SYNC_TIMEOUT_MS = 5000;
static constexpr const char* LOG_TIMEZONE = "PST8PDT,M3.2.0,M11.1.0";
static constexpr const char* LOG_NTP_SERVER_1 = "pool.ntp.org";
static constexpr const char* LOG_NTP_SERVER_2 = "time.nist.gov";

static constexpr const char* APP_TITLE = "ESP32 Web UI Ping Demo";
static constexpr const char* HOMEKIT_DEVICE_NAME = "Mitsubishi AC";
static constexpr const char* HOMEKIT_BRIDGE_NAME = "Mitsubishi AC Bridge";
static constexpr const char* HOMEKIT_MANUFACTURER = "dkt smart home";
static constexpr const char* HOMEKIT_BRIDGE_MODEL = "Mitsubishi CN105 Bridge";
static constexpr const char* HOMEKIT_DEVICE_MODEL = "Mitsubishi Heat Pump";
static constexpr const char* HOMEKIT_FIRMWARE_REVISION = "0.1.0";
static constexpr const char* HOMEKIT_PAIRING_CODE = "88060529";
static constexpr const char* HOMEKIT_QR_ID = "AC01";
static constexpr uint8_t HOMEKIT_CATEGORY = 21;
static constexpr uint8_t HOMEKIT_QR_PROTOCOL_IP = 2;
static constexpr int HOMEKIT_LOG_LEVEL = 1;
static constexpr const char* PING_MESSAGE = "pong from esp32";
static constexpr const char* SERIAL_MESSAGE_PREFIX = "[WebUI] Serial message: ";
static const float DEFAULT_TARGET_TEMPERATURE_F = 77.0f;
static const float DEFAULT_ROOM_TEMPERATURE_F = 74.0f;
static const float DEFAULT_OUTSIDE_TEMPERATURE_F = 64.0f;
static const int MIN_TARGET_TEMPERATURE_F = 61;
static const int MAX_TARGET_TEMPERATURE_F = 88;
static const int TARGET_TEMPERATURE_STEP_F = 1;

#ifndef APP_SECRET_WIFI_SSID
#define APP_SECRET_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef APP_SECRET_WIFI_PASSWORD
#define APP_SECRET_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

static constexpr const char* WIFI_SSID = APP_SECRET_WIFI_SSID;
static constexpr const char* WIFI_PASSWORD = APP_SECRET_WIFI_PASSWORD;

static constexpr const char* FALLBACK_AP_SSID = "Mitsubishi-WebUI";
static constexpr const char* FALLBACK_AP_PASSWORD = "12345678";

static constexpr bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static constexpr bool isAlphaNumeric(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

static constexpr size_t stringLength(const char* value) {
    size_t len = 0;
    while (value[len] != '\0') {
        len++;
    }
    return len;
}

static constexpr int digitCount(const char* value) {
    int count = 0;
    for (size_t i = 0; value[i] != '\0'; i++) {
        if (isDigit(value[i])) {
            count++;
        }
    }
    return count;
}

static constexpr uint32_t parsePairingCodeDigits(const char* value) {
    uint32_t parsed = 0;
    for (size_t i = 0; value[i] != '\0'; i++) {
        if (isDigit(value[i])) {
            parsed = parsed * 10 + static_cast<uint32_t>(value[i] - '0');
        }
    }
    return parsed;
}

static constexpr bool allPairingDigitsSame(const char* value) {
    char firstDigit = '\0';
    bool hasDigit = false;
    for (size_t i = 0; value[i] != '\0'; i++) {
        if (!isDigit(value[i])) {
            continue;
        }
        if (!hasDigit) {
            firstDigit = value[i];
            hasDigit = true;
            continue;
        }
        if (value[i] != firstDigit) {
            return false;
        }
    }
    return hasDigit;
}

static constexpr bool pairingCodeAllowed(const char* value) {
    uint32_t parsed = parsePairingCodeDigits(value);
    return digitCount(value) == 8 &&
           !allPairingDigitsSame(value) &&
           parsed != 12345678 &&
           parsed != 87654321;
}

static constexpr bool qrIdAllowed(const char* value) {
    if (stringLength(value) != 4) {
        return false;
    }
    for (size_t i = 0; i < 4; i++) {
        if (!isAlphaNumeric(value[i])) {
            return false;
        }
    }
    return true;
}

static constexpr char base36Digit(uint8_t digit) {
    return digit < 10 ? static_cast<char>('0' + digit) : static_cast<char>('A' + digit - 10);
}

static constexpr std::array<char, 21> buildHomeKitSetupPayload(const char* pairingCode,
                                                               const char* qrId,
                                                               uint8_t category) {
    std::array<char, 21> payload = {
        'X', '-', 'H', 'M', ':', '/', '/',
        '0', '0', '0', '0', '0', '0', '0', '0', '0',
        ' ', ' ', ' ', ' ', '\0'
    };

    uint64_t payloadWord = (static_cast<uint64_t>(category) << 31) |
                           (static_cast<uint64_t>(HOMEKIT_QR_PROTOCOL_IP) << 27) |
                           parsePairingCodeDigits(pairingCode);

    for (int i = 15; i >= 7; i--) {
        payload[i] = base36Digit(payloadWord % 36);
        payloadWord /= 36;
    }

    for (size_t i = 0; i < 4; i++) {
        payload[16 + i] = qrId[i] == '\0' ? ' ' : qrId[i];
    }

    return payload;
}

static_assert(pairingCodeAllowed(HOMEKIT_PAIRING_CODE), "HomeKit pairing code must be 8 digits and not too simple");
static_assert(qrIdAllowed(HOMEKIT_QR_ID), "HomeKit QR setup ID must be exactly 4 alphanumeric characters");

static constexpr auto HOMEKIT_SETUP_PAYLOAD = buildHomeKitSetupPayload(
    HOMEKIT_PAIRING_CODE,
    HOMEKIT_QR_ID,
    HOMEKIT_CATEGORY
);

}
