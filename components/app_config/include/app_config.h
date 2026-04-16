#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#if __has_include("app_config_local.h")
#include "app_config_local.h"
#endif

#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef APP_WIFI_PASSWORD
#define APP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

namespace app_config {

inline constexpr char kDeviceName[] = "Mitsubishi Heat Pump HomeKit";
inline constexpr char kPhaseName[] = "Phase 3 WiFi service foundation";

inline constexpr esp_log_level_t kDefaultLogLevel = ESP_LOG_INFO;
inline constexpr uint32_t kHeartbeatIntervalMs = 5000;

inline constexpr char kSpiffsBasePath[] = "/spiffs";
inline constexpr char kSpiffsPartitionLabel[] = "spiffs";
inline constexpr int kSpiffsMaxOpenFiles = 5;
inline constexpr char kPersistentLogPath[] = "/spiffs/latest.log";

inline constexpr char kWifiSsid[] = APP_WIFI_SSID;
inline constexpr char kWifiPassword[] = APP_WIFI_PASSWORD;
inline constexpr bool kWifiDisablePowerSave = true;
inline constexpr uint32_t kWifiConnectTimeoutMs = 15000;
inline constexpr uint32_t kWifiReconnectIntervalMs = 10000;

inline constexpr uart_port_t kCn105UartPort = UART_NUM_1;
inline constexpr gpio_num_t kCn105RxPin = GPIO_NUM_26;
inline constexpr gpio_num_t kCn105TxPin = GPIO_NUM_32;
inline constexpr int kCn105BaudRate = 2400;
inline constexpr uart_word_length_t kCn105DataBits = UART_DATA_8_BITS;
inline constexpr uart_parity_t kCn105Parity = UART_PARITY_EVEN;
inline constexpr uart_stop_bits_t kCn105StopBits = UART_STOP_BITS_1;
inline constexpr int kCn105RxBufferBytes = 256;
inline constexpr int kCn105TxBufferBytes = 256;

}  // namespace app_config
