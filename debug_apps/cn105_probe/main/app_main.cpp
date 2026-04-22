#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

static const char* TAG = "cn105_probe";

constexpr uart_port_t kConsoleUart = UART_NUM_0;
constexpr uart_port_t kCn105Uart = UART_NUM_1;

constexpr int kConsoleBaud = 115200;
constexpr int kCn105Baud = 2400;

// Default mapping matches the user's fixed Atom Lite cable:
//   CN105 pin 4 -> GPIO26
//   CN105 pin 5 -> GPIO32
constexpr gpio_num_t kCn105RxPinPrimary = GPIO_NUM_32;
constexpr gpio_num_t kCn105TxPinPrimary = GPIO_NUM_26;
constexpr gpio_num_t kCn105RxPinSwapped = GPIO_NUM_26;
constexpr gpio_num_t kCn105TxPinSwapped = GPIO_NUM_32;

constexpr int kConsoleRxBufferBytes = 256;
constexpr int kCn105RxBufferBytes = 256;
constexpr int kCn105TxBufferBytes = 256;
constexpr uint32_t kRxByteTimeoutMs = 120;
constexpr TickType_t kLoopDelayTicks = pdMS_TO_TICKS(10);
constexpr TickType_t kInfoBurstDelayTicks = pdMS_TO_TICKS(180);
constexpr TickType_t kHeartbeatIntervalTicks = pdMS_TO_TICKS(5000);

constexpr size_t kPacketLen = 22;
constexpr size_t kConnectLen = 8;
constexpr size_t kHeaderLen = 8;
constexpr size_t kInfoHeaderLen = 5;

constexpr uint8_t kHeader[kHeaderLen] = {
    0xFC, 0x41, 0x01, 0x30, 0x10, 0x01, 0x00, 0x00
};

constexpr uint8_t kInfoHeader[kInfoHeaderLen] = {
    0xFC, 0x42, 0x01, 0x30, 0x10
};

constexpr uint8_t kControlPacket1[5] = {
    0x01, 0x02, 0x04, 0x08, 0x10
};

constexpr uint8_t kControlPacket2[1] = {
    0x01
};

constexpr uint8_t kInfoCodes[4] = { 0x02, 0x03, 0x06, 0x09 };
constexpr const char* kInfoLabels[4] = { "0x02", "0x03", "0x06", "0x09" };

struct ProbeState {
    uint8_t rx_buf[kPacketLen] = {};
    size_t rx_index = 0;
    size_t rx_expected = 0;
    uint32_t last_rx_byte_ms = 0;

    bool connected = false;
    bool use_primary_pins = true;
    gpio_num_t current_rx_pin = kCn105RxPinPrimary;
    gpio_num_t current_tx_pin = kCn105TxPinPrimary;
    int next_info_index = 0;

    uint32_t connect_attempts = 0;
    uint32_t info_requests = 0;
    uint32_t set_requests = 0;
    uint32_t rx_packets = 0;
    uint32_t rx_errors = 0;
} state;

uint8_t checksum(const uint8_t* bytes, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += bytes[i];
    }
    return static_cast<uint8_t>((0xFC - sum) & 0xFF);
}

uint8_t encode_high_precision_temp_byte(float temp_c) {
    int encoded = static_cast<int>(temp_c * 2.0f + 128.5f);
    if (encoded < 0) encoded = 0;
    if (encoded > 255) encoded = 255;
    return static_cast<uint8_t>(encoded);
}

void bytes_to_hex(const uint8_t* bytes, size_t len, char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';

    size_t offset = 0;
    for (size_t i = 0; i < len && offset + 4 < out_len; ++i) {
        const int written = snprintf(out + offset, out_len - offset, "%s%02X", i == 0 ? "" : " ", bytes[i]);
        if (written <= 0) {
            break;
        }
        offset += static_cast<size_t>(written);
    }
}

void log_packet(const char* prefix, const uint8_t* bytes, size_t len) {
    char hex[kPacketLen * 3 + 1] = {};
    bytes_to_hex(bytes, len, hex, sizeof(hex));
    ESP_LOGI(TAG, "%s %s", prefix, hex);
}

void print_menu() {
    printf("\n=== CN105 PROBE MENU ===\n");
    printf("0 = Swap CN105 RX/TX pins and reopen UART\n");
    printf("1 = Send CONNECT 0x5A\n");
    printf("2 = Send CONNECT 0x5B\n");
    printf("3 = Send one INFO request (cycles 0x02/0x03/0x06/0x09)\n");
    printf("4 = Send SET ON COOL 75F AUTO AUTO WIDE_CENTER\n");
    printf("5 = Send INFO burst 0x02 + 0x03 + 0x06 + 0x09\n");
    printf("h/? = Print this menu\n\n");
    fflush(stdout);
}

void reset_rx() {
    state.rx_index = 0;
    state.rx_expected = 0;
}

size_t expected_len_for_command(uint8_t command) {
    if (command == 0x7A || command == 0x7B) {
        return kConnectLen;
    }
    return 0;
}

void print_status() {
    ESP_LOGI(TAG,
             "STAT connected=%s rx=%d tx=%d mode=%s connectAttempts=%lu infoRequests=%lu setRequests=%lu rxPackets=%lu rxErrors=%lu nextInfo=%s",
             state.connected ? "yes" : "no",
             static_cast<int>(state.current_rx_pin),
             static_cast<int>(state.current_tx_pin),
             state.use_primary_pins ? "primary" : "swapped",
             static_cast<unsigned long>(state.connect_attempts),
             static_cast<unsigned long>(state.info_requests),
             static_cast<unsigned long>(state.set_requests),
             static_cast<unsigned long>(state.rx_packets),
             static_cast<unsigned long>(state.rx_errors),
             kInfoLabels[state.next_info_index]);
}

void reopen_cn105_uart() {
    uart_flush_input(kCn105Uart);
    ESP_ERROR_CHECK(uart_set_pin(kCn105Uart,
                                 state.current_tx_pin,
                                 state.current_rx_pin,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    state.connected = false;
    reset_rx();

    ESP_LOGI(TAG,
             "CN105 UART reopened: uart=%d rx=%d tx=%d baud=%d format=8E1",
             static_cast<int>(kCn105Uart),
             static_cast<int>(state.current_rx_pin),
             static_cast<int>(state.current_tx_pin),
             kCn105Baud);
}

void send_packet(const char* label, const uint8_t* bytes, size_t len) {
    log_packet(label, bytes, len);
    const int written = uart_write_bytes(kCn105Uart, bytes, len);
    if (written >= 0) {
        uart_wait_tx_done(kCn105Uart, pdMS_TO_TICKS(200));
    }
}

void send_connect_packet(uint8_t command) {
    uint8_t packet[kConnectLen] = { 0xFC, command, 0x01, 0x30, 0x02, 0xCA, 0x01, 0x00 };
    packet[kConnectLen - 1] = checksum(packet, kConnectLen - 1);

    state.connect_attempts++;
    state.connected = false;

    send_packet(command == 0x5B ? "TX CONNECT(0x5B)" : "TX CONNECT(0x5A)", packet, kConnectLen);
}

void send_info_packet(uint8_t info_code) {
    uint8_t packet[kPacketLen] = {};
    memcpy(packet, kInfoHeader, kInfoHeaderLen);
    packet[5] = info_code;
    packet[kPacketLen - 1] = checksum(packet, kPacketLen - 1);

    char label[32] = {};
    snprintf(label, sizeof(label), "TX INFO(0x%02X)", info_code);
    send_packet(label, packet, kPacketLen);
    state.info_requests++;
}

void send_next_info_packet() {
    const uint8_t info_code = kInfoCodes[state.next_info_index];
    send_info_packet(info_code);
    state.next_info_index = (state.next_info_index + 1) % 4;
}

void send_set_on_cool_75f() {
    const float target_temp_c = 24.0f;

    uint8_t packet[kPacketLen] = {};
    memcpy(packet, kHeader, kHeaderLen);

    packet[6] = kControlPacket1[0] | kControlPacket1[1] | kControlPacket1[2] |
                kControlPacket1[3] | kControlPacket1[4];
    packet[7] = kControlPacket2[0];
    packet[8] = 0x01;   // ON
    packet[9] = 0x03;   // COOL
    packet[10] = 0x07;  // 24C legacy encoding
    packet[11] = 0x00;  // fan AUTO
    packet[12] = 0x00;  // vane AUTO
    packet[18] = 0x03;  // wide vane center
    packet[19] = encode_high_precision_temp_byte(target_temp_c);
    packet[kPacketLen - 1] = checksum(packet, kPacketLen - 1);

    state.set_requests++;
    send_packet("TX SET(ON COOL 75F)", packet, kPacketLen);
}

void decode_summary(const uint8_t* bytes, size_t len) {
    if (len < 2) {
        return;
    }

    const uint8_t cmd = bytes[1];
    switch (cmd) {
        case 0x7A:
        case 0x7B:
            state.connected = true;
            ESP_LOGI(TAG, "RX Handshake acknowledged with 0x%02X", cmd);
            break;
        case 0x62:
            if (len > 5) {
                ESP_LOGI(TAG, "RX INFO response infoCode=0x%02X", bytes[5]);
            } else {
                ESP_LOGI(TAG, "RX short 0x62 response");
            }
            break;
        case 0x61:
            ESP_LOGI(TAG, "RX SET ACK");
            break;
        default:
            ESP_LOGI(TAG, "RX Received command 0x%02X", cmd);
            break;
    }
}

void handle_packet(const uint8_t* bytes, size_t len) {
    state.rx_packets++;
    log_packet("PKT RX", bytes, len);

    if (len < 2) {
        state.rx_errors++;
        ESP_LOGW(TAG, "RX Packet too short");
        return;
    }

    const uint8_t expected = checksum(bytes, len - 1);
    if (bytes[len - 1] != expected) {
        state.rx_errors++;
        ESP_LOGW(TAG, "RX Checksum mismatch: got=%02X expected=%02X", bytes[len - 1], expected);
        return;
    }

    decode_summary(bytes, len);
}

void process_rx_byte(uint8_t byte) {
    if (state.rx_index == 0 && byte != 0xFC) {
        ESP_LOGI(TAG, "RX Stray byte 0x%02X", byte);
        return;
    }

    state.rx_buf[state.rx_index++] = byte;
    state.last_rx_byte_ms = static_cast<uint32_t>(esp_log_timestamp());

    if (state.rx_index == 2) {
        state.rx_expected = expected_len_for_command(byte);
    }

    if (state.rx_index == 5 && state.rx_expected == 0) {
        state.rx_expected = static_cast<size_t>(state.rx_buf[4]) + 6;
        if (state.rx_expected > kPacketLen) {
            state.rx_errors++;
            ESP_LOGW(TAG, "RX expected length %u exceeds max", static_cast<unsigned>(state.rx_expected));
            reset_rx();
            return;
        }
    }

    if (state.rx_expected > 0 && state.rx_index >= state.rx_expected) {
        handle_packet(state.rx_buf, state.rx_index);
        reset_rx();
    }

    if (state.rx_index >= kPacketLen) {
        state.rx_errors++;
        ESP_LOGW(TAG, "RX buffer overflow");
        reset_rx();
    }
}

void drain_cn105_rx() {
    uint8_t byte = 0;
    while (uart_read_bytes(kCn105Uart, &byte, 1, 0) == 1) {
        process_rx_byte(byte);
    }
}

void check_rx_timeout() {
    if (state.rx_index == 0) {
        return;
    }
    const uint32_t now_ms = static_cast<uint32_t>(esp_log_timestamp());
    if ((now_ms - state.last_rx_byte_ms) < kRxByteTimeoutMs) {
        return;
    }

    log_packet("PKT RX partial timeout", state.rx_buf, state.rx_index);
    state.rx_errors++;
    reset_rx();
}

void send_info_burst() {
    ESP_LOGI(TAG, "DBG Sending INFO burst: 0x02, 0x03, 0x06, 0x09");
    for (int i = 0; i < 4; ++i) {
        send_info_packet(kInfoCodes[i]);
        vTaskDelay(kInfoBurstDelayTicks);
        drain_cn105_rx();
        check_rx_timeout();
    }
}

void handle_console_command(char c) {
    switch (c) {
        case '0':
            state.use_primary_pins = !state.use_primary_pins;
            state.current_rx_pin = state.use_primary_pins ? kCn105RxPinPrimary : kCn105RxPinSwapped;
            state.current_tx_pin = state.use_primary_pins ? kCn105TxPinPrimary : kCn105TxPinSwapped;
            ESP_LOGI(TAG, "CMD Switching UART pinout to rx=%d tx=%d",
                     static_cast<int>(state.current_rx_pin),
                     static_cast<int>(state.current_tx_pin));
            reopen_cn105_uart();
            print_status();
            break;
        case '1':
            send_connect_packet(0x5A);
            break;
        case '2':
            send_connect_packet(0x5B);
            break;
        case '3':
            send_next_info_packet();
            break;
        case '4':
            send_set_on_cool_75f();
            break;
        case '5':
            send_info_burst();
            break;
        case 'h':
        case 'H':
        case '?':
            print_menu();
            print_status();
            break;
        case '\r':
        case '\n':
        case ' ':
        case '\t':
            break;
        default:
            ESP_LOGI(TAG, "CMD Unknown command '%c' (0x%02X)", c, static_cast<uint8_t>(c));
            print_menu();
            break;
    }
}

void drain_console_commands() {
    uint8_t ch = 0;
    while (uart_read_bytes(kConsoleUart, &ch, 1, 0) == 1) {
        handle_console_command(static_cast<char>(ch));
    }
}

void init_console_uart() {
    uart_config_t config = {};
    config.baud_rate = kConsoleBaud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.rx_flow_ctrl_thresh = 0;
    config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(kConsoleUart, kConsoleRxBufferBytes, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(kConsoleUart, &config));
}

void init_cn105_uart() {
    uart_config_t config = {};
    config.baud_rate = kCn105Baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_EVEN;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.rx_flow_ctrl_thresh = 0;
    config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(kCn105Uart, kCn105RxBufferBytes, kCn105TxBufferBytes, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(kCn105Uart, &config));
    reopen_cn105_uart();
}

}  // namespace

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(200));
    init_console_uart();
    init_cn105_uart();

    printf("\n=== CN105 Probe (ESP-IDF) ===\n");
    printf("Console UART: %d @ %d 8N1\n", static_cast<int>(kConsoleUart), kConsoleBaud);
    printf("CN105 UART: %d @ %d 8E1, default rx=%d tx=%d\n",
           static_cast<int>(kCn105Uart),
           kCn105Baud,
           static_cast<int>(state.current_rx_pin),
           static_cast<int>(state.current_tx_pin));
    printf("Mode: fully manual. Nothing is sent until you type a command.\n");
    fflush(stdout);

    print_menu();
    print_status();
    ESP_LOGI(TAG, "BOOT Probe ready");

    TickType_t last_heartbeat = xTaskGetTickCount();
    while (true) {
        drain_console_commands();
        drain_cn105_rx();
        check_rx_timeout();

        const TickType_t now = xTaskGetTickCount();
        if ((now - last_heartbeat) >= kHeartbeatIntervalTicks) {
            last_heartbeat = now;
            print_status();
        }

        vTaskDelay(kLoopDelayTicks);
    }
}
