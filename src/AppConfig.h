#pragma once

#include <cstdint>

#if __has_include("AppSecrets.h")
#include "AppSecrets.h"
#endif

namespace AppConfig {

static const uint32_t SERIAL_BAUD = 115200;
static const uint16_t WEB_PORT = 80;
static const uint32_t CN105_UART_BAUD = 2400;
static const int CN105_UART_PORT = 1;
static const int CN105_RX_PIN = 32;
static const int CN105_TX_PIN = 26;
static const uint32_t CN105_RX_TIMEOUT_MS = 120;
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static const bool WIFI_DISABLE_SLEEP = true;
static const uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
static const uint32_t HEARTBEAT_INTERVAL_MS = 5000;

static constexpr const char* APP_TITLE = "ESP32 Web UI Ping Demo";
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

}
