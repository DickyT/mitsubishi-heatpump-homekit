#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_private/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

static const char* TAG = "cn105_probe";

constexpr uart_port_t kConsoleUart = UART_NUM_0;
constexpr uart_port_t kCn105Uart = UART_NUM_1;

constexpr int kConsoleBaud = 115200;
constexpr int kCn105BaudOptions[] = { 2400, 4800, 9600 };
constexpr size_t kCn105BaudOptionCount = sizeof(kCn105BaudOptions) / sizeof(kCn105BaudOptions[0]);

// Default mapping matches the user's fixed Atom Lite cable and the main IDF
// firmware config:
//   CN105 pin 4 -> GPIO26 (ESP RX)
//   CN105 pin 5 -> GPIO32 (ESP TX)
constexpr gpio_num_t kCn105RxPinPrimary = GPIO_NUM_26;
constexpr gpio_num_t kCn105TxPinPrimary = GPIO_NUM_32;
constexpr gpio_num_t kCn105RxPinSwapped = GPIO_NUM_32;
constexpr gpio_num_t kCn105TxPinSwapped = GPIO_NUM_26;

constexpr int kConsoleRxBufferBytes = 256;
constexpr int kCn105RxBufferBytes = 256;
constexpr int kCn105TxBufferBytes = 256;
constexpr uint32_t kRxByteTimeoutMs = 120;
constexpr TickType_t kLoopDelayTicks = pdMS_TO_TICKS(10);
constexpr TickType_t kInfoBurstDelayTicks = pdMS_TO_TICKS(180);
constexpr TickType_t kHeartbeatIntervalTicks = pdMS_TO_TICKS(5000);
constexpr TickType_t kAutoScanSettleTicks = pdMS_TO_TICKS(120);
constexpr TickType_t kAutoScanProbeTicks = pdMS_TO_TICKS(700);

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
    uint8_t electrical_profile = 1;
    size_t baud_index = 0;
    gpio_num_t current_rx_pin = kCn105RxPinPrimary;
    gpio_num_t current_tx_pin = kCn105TxPinPrimary;
    int next_info_index = 0;

    uint32_t connect_attempts = 0;
    uint32_t info_requests = 0;
    uint32_t set_requests = 0;
    uint32_t rx_bytes = 0;
    uint32_t rx_packets = 0;
    uint32_t rx_errors = 0;
} state;

struct AutoScanResult {
    bool use_primary_pins = true;
    uint8_t electrical_profile = 0;
    size_t baud_index = 0;
    uint8_t connect_command = 0x5A;
    uint32_t rx_bytes = 0;
    uint32_t rx_packets = 0;
    uint32_t rx_errors = 0;
    bool connected = false;
};

constexpr size_t kAutoScanResultCount = 2 * kCn105BaudOptionCount * 4 * 2;

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
    printf("4 = Send SET POWER ON COOL 75F AUTO AUTO WIDE_CENTER\n");
    printf("5 = Send SET POWER OFF\n");
    printf("6 = Cycle electrical profile: push-pull / push-pull+RX_PULLUP / OD+RX_PULLUP / OD+TX_RX_PULLUP\n");
    printf("7 = Cycle CN105 baud rate (2400 -> 4800 -> 9600)\n");
    printf("8 = Auto-detect best pinout/baud/profile/connect combo and keep it active\n");
    printf("9 = Send INFO burst 0x02 + 0x03 + 0x06 + 0x09 using current detected config\n");
    printf("h/? = Print this menu\n\n");
    fflush(stdout);
}

void reset_rx() {
    state.rx_index = 0;
    state.rx_expected = 0;
}

size_t expected_len_for_command(uint8_t command) {
    // CONNECT ACK packets are not always 8 bytes long on the wire. Let the
    // generic "payload length + 6" parser handle 0x7A/0x7B using byte[4].
    (void) command;
    return 0;
}

int current_cn105_baud() {
    return kCn105BaudOptions[state.baud_index];
}

const char* electrical_profile_name() {
    switch (state.electrical_profile) {
        case 0:
            return "push-pull";
        case 1:
            return "push-pull+rx-pullup";
        case 2:
            return "open-drain+rx-pullup";
        case 3:
            return "open-drain+tx-rx-pullup";
        default:
            return "unknown";
    }
}

void print_status() {
    ESP_LOGI(TAG,
             "STAT connected=%s rx=%d tx=%d baud=%d pins=%s drive=%s connectAttempts=%lu infoRequests=%lu setRequests=%lu rxBytes=%lu rxPackets=%lu rxErrors=%lu nextInfo=%s",
             state.connected ? "yes" : "no",
             static_cast<int>(state.current_rx_pin),
             static_cast<int>(state.current_tx_pin),
             current_cn105_baud(),
             state.use_primary_pins ? "primary" : "swapped",
             electrical_profile_name(),
             static_cast<unsigned long>(state.connect_attempts),
             static_cast<unsigned long>(state.info_requests),
             static_cast<unsigned long>(state.set_requests),
             static_cast<unsigned long>(state.rx_bytes),
             static_cast<unsigned long>(state.rx_packets),
             static_cast<unsigned long>(state.rx_errors),
             kInfoLabels[state.next_info_index]);
}

void apply_electrical_profile() {
    const bool tx_open_drain = (state.electrical_profile >= 2);
    const bool rx_pullup = (state.electrical_profile >= 1);
    const bool tx_pullup = (state.electrical_profile >= 3);

    // Reconfigure pad behavior after uart_set_pin() wires the UART signal through the GPIO matrix.
    // Do not call gpio_set_direction() here: that can warn on pins already owned by UART.
    if (tx_open_drain) {
        ESP_ERROR_CHECK(gpio_od_enable(state.current_tx_pin));
    } else {
        ESP_ERROR_CHECK(gpio_od_disable(state.current_tx_pin));
    }

    if (tx_pullup) {
        ESP_ERROR_CHECK(gpio_pullup_en(state.current_tx_pin));
    } else {
        ESP_ERROR_CHECK(gpio_pullup_dis(state.current_tx_pin));
    }

    if (rx_pullup) {
        ESP_ERROR_CHECK(gpio_pullup_en(state.current_rx_pin));
    } else {
        ESP_ERROR_CHECK(gpio_pullup_dis(state.current_rx_pin));
    }
}

void reopen_cn105_uart(bool rebind_pins = true) {
    const bool tx_open_drain = (state.electrical_profile >= 2);
    const bool rx_pullup = (state.electrical_profile >= 1);
    const bool tx_pullup = (state.electrical_profile >= 3);

    ESP_ERROR_CHECK(uart_set_baudrate(kCn105Uart, current_cn105_baud()));
    uart_flush_input(kCn105Uart);
    if (rebind_pins) {
        ESP_ERROR_CHECK(uart_set_pin(kCn105Uart,
                                     state.current_tx_pin,
                                     state.current_rx_pin,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE));
    }
    apply_electrical_profile();

    state.connected = false;
    reset_rx();

    ESP_LOGI(TAG,
             "CN105 UART reopened: uart=%d rx=%d tx=%d baud=%d format=8E1 drive=%s",
             static_cast<int>(kCn105Uart),
             static_cast<int>(state.current_rx_pin),
             static_cast<int>(state.current_tx_pin),
             current_cn105_baud(),
             electrical_profile_name());
    ESP_LOGI(TAG,
             "CN105 electrical profile: tx_open_drain=%s rx_pullup=%s tx_pullup=%s",
             tx_open_drain ? "yes" : "no",
             rx_pullup ? "yes" : "no",
             tx_pullup ? "yes" : "no");
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

void send_set_packet(bool power_on) {
    uint8_t packet[kPacketLen] = {};
    memcpy(packet, kHeader, kHeaderLen);

    if (power_on) {
        const float target_temp_c = 24.0f;
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
    } else {
        // Minimal power-off SET: only request the power bit to change.
        packet[6] = kControlPacket1[0];
        packet[8] = 0x00;  // OFF
    }
    packet[kPacketLen - 1] = checksum(packet, kPacketLen - 1);

    state.set_requests++;
    send_packet(power_on ? "TX SET(POWER ON COOL 75F)" : "TX SET(POWER OFF)", packet, kPacketLen);
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
        state.rx_bytes++;
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

void apply_current_pinout_from_state() {
    state.current_rx_pin = state.use_primary_pins ? kCn105RxPinPrimary : kCn105RxPinSwapped;
    state.current_tx_pin = state.use_primary_pins ? kCn105TxPinPrimary : kCn105TxPinSwapped;
}

void wait_probe_window(TickType_t duration_ticks) {
    const TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < duration_ticks) {
        drain_cn105_rx();
        check_rx_timeout();
        vTaskDelay(kLoopDelayTicks);
    }
}

void print_auto_scan_result(const AutoScanResult& result) {
    ESP_LOGI(TAG,
             "AUTO result pins=%s baud=%d profile=%u(%s) connect=0x%02X rxBytes=%lu rxPackets=%lu rxErrors=%lu connected=%s",
             result.use_primary_pins ? "primary" : "swapped",
             kCn105BaudOptions[result.baud_index],
             static_cast<unsigned>(result.electrical_profile),
             (result.electrical_profile == 0) ? "push-pull" :
             (result.electrical_profile == 1) ? "push-pull+rx-pullup" :
             (result.electrical_profile == 2) ? "open-drain+rx-pullup" :
             "open-drain+tx-rx-pullup",
             result.connect_command,
             static_cast<unsigned long>(result.rx_bytes),
             static_cast<unsigned long>(result.rx_packets),
             static_cast<unsigned long>(result.rx_errors),
             result.connected ? "yes" : "no");
}

void run_auto_scan() {
    ESP_LOGI(TAG, "AUTO Starting CN105 auto-scan across pinouts, baud rates, electrical profiles, and CONNECT variants");

    AutoScanResult results[kAutoScanResultCount] = {};
    size_t result_index = 0;
    bool last_pin_mode_primary = state.use_primary_pins;

    for (int pin_mode = 0; pin_mode < 2; ++pin_mode) {
        state.use_primary_pins = (pin_mode == 0);
        apply_current_pinout_from_state();

        for (size_t baud_idx = 0; baud_idx < kCn105BaudOptionCount; ++baud_idx) {
            state.baud_index = baud_idx;

            for (uint8_t profile = 0; profile < 4; ++profile) {
                state.electrical_profile = profile;
                const bool pinout_changed = (pin_mode == 0 && !last_pin_mode_primary) ||
                                            (pin_mode == 1 && last_pin_mode_primary);
                reopen_cn105_uart(pinout_changed);
                last_pin_mode_primary = state.use_primary_pins;
                wait_probe_window(kAutoScanSettleTicks);

                static const uint8_t connect_commands[2] = { 0x5A, 0x5B };
                for (uint8_t command : connect_commands) {
                    const uint32_t before_bytes = state.rx_bytes;
                    const uint32_t before_packets = state.rx_packets;
                    const uint32_t before_errors = state.rx_errors;
                    const bool before_connected = state.connected;

                    ESP_LOGI(TAG,
                             "AUTO probing pins=%s baud=%d profile=%s connect=0x%02X",
                             state.use_primary_pins ? "primary" : "swapped",
                             current_cn105_baud(),
                             electrical_profile_name(),
                             command);

                    send_connect_packet(command);
                    wait_probe_window(kAutoScanProbeTicks);

                    AutoScanResult& result = results[result_index++];
                    result.use_primary_pins = state.use_primary_pins;
                    result.electrical_profile = state.electrical_profile;
                    result.baud_index = state.baud_index;
                    result.connect_command = command;
                    result.rx_bytes = state.rx_bytes - before_bytes;
                    result.rx_packets = state.rx_packets - before_packets;
                    result.rx_errors = state.rx_errors - before_errors;
                    result.connected = (!before_connected && state.connected) || state.connected;

                    print_auto_scan_result(result);

                    state.connected = false;
                    reset_rx();
                    uart_flush_input(kCn105Uart);
                }
            }
        }
    }

    size_t positive_results = 0;
    int best_index = -1;
    uint32_t best_score = 0;

    ESP_LOGI(TAG, "AUTO scan complete. Summary of combinations with any RX:");
    for (size_t i = 0; i < result_index; ++i) {
        const AutoScanResult& result = results[i];
        const bool positive = result.rx_bytes > 0 || result.rx_packets > 0 || result.connected;
        if (!positive) {
            continue;
        }

        positive_results++;
        const uint32_t score = (result.connected ? 1000000u : 0u) +
                               (result.rx_packets * 1000u) +
                               result.rx_bytes;
        if (best_index < 0 || score > best_score) {
            best_index = static_cast<int>(i);
            best_score = score;
        }
        print_auto_scan_result(result);
    }

    if (positive_results == 0) {
        ESP_LOGW(TAG, "AUTO no combinations produced any RX bytes");
    } else if (best_index >= 0) {
        ESP_LOGI(TAG, "AUTO best match:");
        print_auto_scan_result(results[best_index]);
        state.use_primary_pins = results[best_index].use_primary_pins;
        state.electrical_profile = results[best_index].electrical_profile;
        state.baud_index = results[best_index].baud_index;
        apply_current_pinout_from_state();
        reopen_cn105_uart(true);
        ESP_LOGI(TAG, "AUTO best match is now active; use commands 1/2/3/4/5/9 with this config");
        print_status();
        return;
    }
    print_status();
}

void handle_console_command(char c) {
    switch (c) {
        case '0':
            state.use_primary_pins = !state.use_primary_pins;
            apply_current_pinout_from_state();
            ESP_LOGI(TAG, "CMD Switching UART pinout to rx=%d tx=%d",
                     static_cast<int>(state.current_rx_pin),
                     static_cast<int>(state.current_tx_pin));
            reopen_cn105_uart(true);
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
            send_set_packet(true);
            break;
        case '5':
            send_set_packet(false);
            break;
        case '6':
            state.electrical_profile = (state.electrical_profile + 1) % 4;
            ESP_LOGI(TAG, "CMD Switching CN105 electrical profile to %s", electrical_profile_name());
            reopen_cn105_uart(false);
            print_status();
            break;
        case '7':
            state.baud_index = (state.baud_index + 1) % kCn105BaudOptionCount;
            ESP_LOGI(TAG, "CMD Switching CN105 baud rate to %d", current_cn105_baud());
            reopen_cn105_uart(false);
            print_status();
            break;
        case '8':
            run_auto_scan();
            break;
        case '9':
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
    config.baud_rate = current_cn105_baud();
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
           current_cn105_baud(),
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
