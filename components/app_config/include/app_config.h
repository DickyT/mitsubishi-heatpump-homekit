#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

namespace app_config {

inline constexpr char kDeviceName[] = "Mitsubishi Heat Pump Matter";
inline constexpr char kPhaseName[] = "Phase 1 platform services skeleton";

inline constexpr esp_log_level_t kDefaultLogLevel = ESP_LOG_INFO;
inline constexpr uint32_t kHeartbeatIntervalMs = 5000;

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
