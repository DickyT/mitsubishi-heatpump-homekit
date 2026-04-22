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
inline constexpr char kPhaseName[] = "ESP-IDF stable baseline";

inline constexpr esp_log_level_t kDefaultLogLevel = ESP_LOG_VERBOSE;
inline constexpr uint32_t kHeartbeatIntervalMs = 5000;
inline constexpr uint16_t kWebServerPort = 8080;
inline constexpr uint32_t kWebServerStackBytes = 12288;
inline constexpr uint8_t kWebServerMaxOpenSockets = 3;
inline constexpr uint8_t kWebServerMaxUriHandlers = 32;
inline constexpr size_t kWebFileListMaxItems = 8;
inline constexpr bool kStatusLedEnabled = true;
inline constexpr gpio_num_t kStatusLedPin = GPIO_NUM_27;
inline constexpr size_t kStatusLedPixels = 1;
inline constexpr uint8_t kStatusLedBrightness = 24;
inline constexpr uint32_t kStatusLedUpdateIntervalMs = 100;
inline constexpr uint32_t kStatusLedBlinkPeriodMs = 400;
inline constexpr uint32_t kStatusLedCommHoldMs = 700;
inline constexpr size_t kStatusLedTaskStackBytes = 3072;

inline constexpr bool kHomeKitEnabled = true;
inline constexpr char kHomeKitAccessoryName[] = "Mitsubishi AC";
inline constexpr char kHomeKitManufacturer[] = "dkt smart home";
inline constexpr char kHomeKitModel[] = "Mitsubishi Heat Pump";
inline constexpr char kHomeKitSerialNumber[] = "DKT-MITSUBISHI-HOMEKIT";
inline constexpr char kHomeKitFirmwareRevision[] = "0.7.0";
inline constexpr char kHomeKitHardwareRevision[] = "1.0";
inline constexpr char kHomeKitSetupCode[] = "111-22-333";
inline constexpr char kHomeKitSetupId[] = "DKT1";

inline constexpr char kSpiffsBasePath[] = "/spiffs";
inline constexpr char kSpiffsPartitionLabel[] = "spiffs";
inline constexpr int kSpiffsMaxOpenFiles = 8;

inline constexpr bool kPersistentLogEnabled = true;
inline constexpr size_t kPersistentLogMaxTotalBytes = 192 * 1024;
inline constexpr size_t kPersistentLogBufferBytes = 4096;
inline constexpr size_t kPersistentLogLineBytes = 256;
inline constexpr size_t kPersistentLogReadChunkBytes = 1024;
inline constexpr uint32_t kPersistentLogFlushIntervalMs = 30000;
inline constexpr size_t kPersistentLogPruneIntervalBytes = 4096;
inline constexpr char kPersistentLogTimezone[] = "PST8PDT,M3.2.0,M11.1.0";

inline constexpr char kWifiSsid[] = APP_WIFI_SSID;
inline constexpr char kWifiPassword[] = APP_WIFI_PASSWORD;
inline constexpr bool kWifiDisablePowerSave = true;
inline constexpr uint32_t kWifiConnectTimeoutMs = 15000;
inline constexpr uint32_t kWifiReconnectIntervalMs = 10000;

inline constexpr uart_port_t kCn105UartPort = UART_NUM_1;
inline constexpr gpio_num_t kCn105RxPin = GPIO_NUM_26;
inline constexpr gpio_num_t kCn105TxPin = GPIO_NUM_32;
inline constexpr bool kCn105RxPullupEnabled = true;
inline constexpr int kCn105BaudRate = 2400;
inline constexpr uart_word_length_t kCn105DataBits = UART_DATA_8_BITS;
inline constexpr uart_parity_t kCn105Parity = UART_PARITY_EVEN;
inline constexpr uart_stop_bits_t kCn105StopBits = UART_STOP_BITS_1;
inline constexpr int kCn105RxBufferBytes = 256;
inline constexpr int kCn105TxBufferBytes = 256;

inline constexpr bool kCn105UseRealTransport = true;
inline constexpr uint32_t kCn105ConnectRetryMs = 10000;
inline constexpr uint32_t kCn105PollIntervalActiveMs = 15000;
inline constexpr uint32_t kCn105PollIntervalOffMs = 60000;
inline constexpr uint32_t kCn105ResponseTimeoutMs = 1000;
inline constexpr uint32_t kCn105RxByteTimeoutMs = 120;
inline constexpr size_t kCn105TransportStackBytes = 4096;

}  // namespace app_config
