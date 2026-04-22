#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

namespace {

const char* TAG = "installer_probe";

constexpr uart_port_t kCn105Uart = UART_NUM_1;
constexpr int kCn105RxBufferBytes = 256;
constexpr int kCn105TxBufferBytes = 256;
constexpr int kBaudOptions[] = {2400, 4800, 9600};
constexpr size_t kPacketLen = 22;
constexpr TickType_t kProbeWindowTicks = pdMS_TO_TICKS(900);
constexpr TickType_t kProbeSettleTicks = pdMS_TO_TICKS(120);
constexpr TickType_t kLoopDelayTicks = pdMS_TO_TICKS(10);
constexpr uint32_t kRxByteTimeoutMs = 140;
constexpr int WIFI_CONNECTED_BIT = BIT0;
constexpr const char* kDeviceCfgNamespace = "device_cfg";
constexpr const char* kProvisioningPop = "abcd1234";

EventGroupHandle_t wifi_event_group = nullptr;
httpd_handle_t server = nullptr;
char wifi_ip[16] = "0.0.0.0";
char wifi_ssid[33] = "";
char wifi_password[65] = "";
bool wifi_connected = false;
const esp_partition_t* pending_ota_partition = nullptr;
led_strip_handle_t test_led = nullptr;
int test_led_pin = -1;

struct InstallerSettings {
    char device_name[64] = "Mitsubishi AC";
    char wifi_ssid[33] = "YOUR_WIFI_SSID";
    char wifi_pass[65] = "YOUR_WIFI_PASSWORD";
    char hk_code[9] = "11112233";
    char hk_mfr[64] = "dkt smart home";
    char hk_model[64] = "Mitsubishi Heat Pump";
    char hk_serial[64] = "DKT-MITSUBISHI-HOMEKIT";
    char hk_setupid[5] = "DKT1";
    bool use_real = true;
    bool led_on = true;
    int led_pin = 27;
    int rx_pin = 26;
    int tx_pin = 32;
    int baud = 2400;
    int data_bits = 8;
    char parity = 'E';
    int stop_bits = 1;
    bool rx_pull = true;
    bool tx_od = false;
    uint32_t poll_on = 15000;
    uint32_t poll_off = 60000;
    uint8_t log_level = ESP_LOG_ERROR;
};

struct ProbeResult {
    bool ran = false;
    bool found = false;
    int rx_pin = 26;
    int tx_pin = 32;
    int baud = 2400;
    uint8_t profile = 1;
    uint8_t connect = 0x5A;
    uint32_t rx_bytes = 0;
    uint32_t rx_packets = 0;
    uint32_t rx_errors = 0;
    char summary[192] = "Not run yet";
};

struct ProbeRuntime {
    uint8_t rx_buf[kPacketLen] = {};
    size_t rx_index = 0;
    size_t rx_expected = 0;
    uint32_t last_rx_byte_ms = 0;
    uint32_t rx_bytes = 0;
    uint32_t rx_packets = 0;
    uint32_t rx_errors = 0;
    bool legal_response = false;
};

ProbeResult last_probe;

uint8_t checksum(const uint8_t* bytes, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += bytes[i];
    }
    return static_cast<uint8_t>((0xFC - sum) & 0xFF);
}

bool falseLike(const char* value) {
    return value != nullptr &&
           (std::strcmp(value, "0") == 0 ||
            std::strcmp(value, "false") == 0 ||
            std::strcmp(value, "off") == 0);
}

void copyString(char* out, size_t out_len, const char* value) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    std::snprintf(out, out_len, "%s", value == nullptr ? "" : value);
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void urlDecodeInPlace(char* value, size_t value_len) {
    if (value == nullptr || value_len == 0) {
        return;
    }
    size_t out = 0;
    for (size_t in = 0; value[in] != '\0' && out < value_len - 1; ++in) {
        if (value[in] == '%' && value[in + 1] != '\0' && value[in + 2] != '\0') {
            const int high = hexNibble(value[in + 1]);
            const int low = hexNibble(value[in + 2]);
            if (high >= 0 && low >= 0) {
                value[out++] = static_cast<char>((high << 4) | low);
                in += 2;
                continue;
            }
        }
        value[out++] = value[in] == '+' ? ' ' : value[in];
    }
    value[out] = '\0';
}

bool formValue(const char* body, const char* key, char* value, size_t value_len) {
    if (body == nullptr || key == nullptr || value == nullptr || value_len == 0) {
        return false;
    }
    if (httpd_query_key_value(body, key, value, value_len) != ESP_OK) {
        return false;
    }
    urlDecodeInPlace(value, value_len);
    return true;
}

bool readBody(httpd_req_t* req, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    int remaining = req->content_len;
    size_t total = 0;
    while (remaining > 0 && total + 1 < out_len) {
        const int received = httpd_req_recv(req, out + total, std::min<int>(remaining, static_cast<int>(out_len - total - 1)));
        if (received <= 0) {
            return false;
        }
        total += static_cast<size_t>(received);
        remaining -= received;
    }
    out[total] = '\0';
    return remaining == 0;
}

std::string jsonEscape(const char* src) {
    std::string out;
    if (src == nullptr) {
        return out;
    }
    for (size_t i = 0; src[i] != '\0'; ++i) {
        const char c = src[i];
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

esp_err_t sendText(httpd_req_t* req, const char* type, const char* body) {
    httpd_resp_set_type(req, type);
    return httpd_resp_sendstr(req, body);
}

esp_err_t sendJsonError(httpd_req_t* req, const char* error) {
    std::string body = "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
    httpd_resp_set_status(req, "400 Bad Request");
    return sendText(req, "application/json", body.c_str());
}

bool validGpio(int value) {
    return value >= 0 && value <= 39;
}

bool validOutputGpio(int value) {
    return value >= 0 && value <= 33;
}

bool validBaud(int value) {
    return value == 2400 || value == 4800 || value == 9600;
}

bool validSetupId(const char* value) {
    if (value == nullptr || std::strlen(value) != 4) {
        return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
            return false;
        }
    }
    return true;
}

bool normalizeHomeKitCode(const char* value, char* out, size_t out_len) {
    if (out == nullptr || out_len < 9) {
        return false;
    }
    size_t count = 0;
    if (value != nullptr) {
        for (size_t i = 0; value[i] != '\0'; ++i) {
            if (value[i] >= '0' && value[i] <= '9') {
                if (count >= 8) return false;
                out[count++] = value[i];
            } else if (value[i] != '-' && value[i] != ' ' && value[i] != '\t') {
                return false;
            }
        }
    }
    if (count != 8) {
        return false;
    }
    out[count] = '\0';
    return true;
}

uart_parity_t parityFromChar(char parity) {
    if (parity == 'N') return UART_PARITY_DISABLE;
    if (parity == 'O') return UART_PARITY_ODD;
    return UART_PARITY_EVEN;
}

uart_stop_bits_t stopBitsFromInt(int stop_bits) {
    return stop_bits == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
}

const char* profileName(uint8_t profile) {
    switch (profile) {
        case 0: return "push-pull";
        case 1: return "push-pull+rx-pullup";
        case 2: return "open-drain+rx-pullup";
        case 3: return "open-drain+tx-rx-pullup";
        default: return "unknown";
    }
}

void configureCn105Pins(int rx_pin, int tx_pin, uint8_t profile) {
    const gpio_num_t rx = static_cast<gpio_num_t>(rx_pin);
    const gpio_num_t tx = static_cast<gpio_num_t>(tx_pin);
    const bool tx_open_drain = profile >= 2;
    const bool rx_pullup = profile >= 1;
    const bool tx_pullup = profile >= 3;

    if (tx_open_drain) {
        gpio_set_direction(tx, GPIO_MODE_OUTPUT_OD);
    } else {
        gpio_set_direction(tx, GPIO_MODE_OUTPUT);
    }

    if (rx_pullup) {
        gpio_pullup_en(rx);
    } else {
        gpio_pullup_dis(rx);
    }
    if (tx_pullup) {
        gpio_pullup_en(tx);
    } else {
        gpio_pullup_dis(tx);
    }
}

esp_err_t reopenCn105Uart(int rx_pin, int tx_pin, int baud, uint8_t profile, char parity = 'E', int stop_bits = 1) {
    uart_driver_delete(kCn105Uart);

    uart_config_t config = {};
    config.baud_rate = baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = parityFromChar(parity);
    config.stop_bits = stopBitsFromInt(stop_bits);
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.rx_flow_ctrl_thresh = 0;
    config.source_clk = UART_SCLK_DEFAULT;

    ESP_RETURN_ON_ERROR(uart_param_config(kCn105Uart, &config), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(kCn105Uart,
                                     static_cast<gpio_num_t>(tx_pin),
                                     static_cast<gpio_num_t>(rx_pin),
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG,
                        "uart_set_pin failed");
    ESP_RETURN_ON_ERROR(uart_driver_install(kCn105Uart, kCn105RxBufferBytes, kCn105TxBufferBytes, 0, nullptr, 0),
                        TAG,
                        "uart_driver_install failed");
    configureCn105Pins(rx_pin, tx_pin, profile);
    uart_flush_input(kCn105Uart);
    return ESP_OK;
}

void resetProbeRx(ProbeRuntime& runtime) {
    runtime.rx_index = 0;
    runtime.rx_expected = 0;
}

void processProbePacket(ProbeRuntime& runtime, const uint8_t* bytes, size_t len) {
    runtime.rx_packets++;
    if (len < 2) {
        runtime.rx_errors++;
        return;
    }
    const uint8_t expected = checksum(bytes, len - 1);
    if (bytes[len - 1] != expected) {
        runtime.rx_errors++;
        return;
    }
    const uint8_t command = bytes[1];
    if (command == 0x7A || command == 0x7B || command == 0x61 || command == 0x62) {
        runtime.legal_response = true;
    }
}

void processProbeByte(ProbeRuntime& runtime, uint8_t byte) {
    if (runtime.rx_index == 0 && byte != 0xFC) {
        return;
    }
    runtime.rx_buf[runtime.rx_index++] = byte;
    runtime.last_rx_byte_ms = esp_log_timestamp();

    if (runtime.rx_index == 5 && runtime.rx_expected == 0) {
        runtime.rx_expected = static_cast<size_t>(runtime.rx_buf[4]) + 6;
        if (runtime.rx_expected > kPacketLen) {
            runtime.rx_errors++;
            resetProbeRx(runtime);
            return;
        }
    }
    if (runtime.rx_expected > 0 && runtime.rx_index >= runtime.rx_expected) {
        processProbePacket(runtime, runtime.rx_buf, runtime.rx_index);
        resetProbeRx(runtime);
    }
    if (runtime.rx_index >= kPacketLen) {
        runtime.rx_errors++;
        resetProbeRx(runtime);
    }
}

void drainProbeRx(ProbeRuntime& runtime) {
    uint8_t byte = 0;
    while (uart_read_bytes(kCn105Uart, &byte, 1, 0) == 1) {
        runtime.rx_bytes++;
        processProbeByte(runtime, byte);
    }
}

void checkProbeTimeout(ProbeRuntime& runtime) {
    if (runtime.rx_index == 0) {
        return;
    }
    if ((esp_log_timestamp() - runtime.last_rx_byte_ms) >= kRxByteTimeoutMs) {
        runtime.rx_errors++;
        resetProbeRx(runtime);
    }
}

void probeWait(ProbeRuntime& runtime, TickType_t ticks) {
    const TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < ticks) {
        drainProbeRx(runtime);
        checkProbeTimeout(runtime);
        vTaskDelay(kLoopDelayTicks);
    }
}

void sendConnect(uint8_t command) {
    uint8_t packet[8] = {0xFC, command, 0x01, 0x30, 0x02, 0xCA, 0x01, 0x00};
    packet[7] = checksum(packet, 7);
    uart_write_bytes(kCn105Uart, packet, sizeof(packet));
    uart_wait_tx_done(kCn105Uart, pdMS_TO_TICKS(200));
}

void sendInfo(uint8_t info_code) {
    uint8_t packet[kPacketLen] = {};
    packet[0] = 0xFC;
    packet[1] = 0x42;
    packet[2] = 0x01;
    packet[3] = 0x30;
    packet[4] = 0x10;
    packet[5] = info_code;
    packet[kPacketLen - 1] = checksum(packet, kPacketLen - 1);
    uart_write_bytes(kCn105Uart, packet, sizeof(packet));
    uart_wait_tx_done(kCn105Uart, pdMS_TO_TICKS(200));
}

ProbeResult runProbe(int rx_pin, int tx_pin, char parity, int stop_bits) {
    ProbeResult best{};
    best.ran = true;
    best.rx_pin = rx_pin;
    best.tx_pin = tx_pin;
    copyString(best.summary, sizeof(best.summary), "No legal CN105 response detected");

    uint32_t best_score = 0;
    static constexpr uint8_t profiles[] = {0, 1, 2, 3};
    static constexpr uint8_t connects[] = {0x5A, 0x5B};

    for (int baud : kBaudOptions) {
        for (uint8_t profile : profiles) {
            if (reopenCn105Uart(rx_pin, tx_pin, baud, profile, parity, stop_bits) != ESP_OK) {
                continue;
            }
            ProbeRuntime settle_runtime{};
            probeWait(settle_runtime, pdMS_TO_TICKS(1));
            vTaskDelay(kProbeSettleTicks);

            for (uint8_t connect : connects) {
                ProbeRuntime runtime{};
                sendConnect(connect);
                probeWait(runtime, kProbeWindowTicks);
                if (!runtime.legal_response) {
                    sendInfo(0x02);
                    probeWait(runtime, kProbeWindowTicks);
                }

                const uint32_t score = (runtime.legal_response ? 1000000u : 0u) +
                                       runtime.rx_packets * 1000u +
                                       runtime.rx_bytes -
                                       runtime.rx_errors * 100u;
                if (score > best_score) {
                    best_score = score;
                    best.found = runtime.legal_response;
                    best.rx_pin = rx_pin;
                    best.tx_pin = tx_pin;
                    best.baud = baud;
                    best.profile = profile;
                    best.connect = connect;
                    best.rx_bytes = runtime.rx_bytes;
                    best.rx_packets = runtime.rx_packets;
                    best.rx_errors = runtime.rx_errors;
                    std::snprintf(best.summary,
                                  sizeof(best.summary),
                                  "%s: baud=%d profile=%s connect=0x%02X rxBytes=%lu rxPackets=%lu rxErrors=%lu",
                                  best.found ? "Legal CN105 response detected" : "Only non-legal/echo bytes detected",
                                  baud,
                                  profileName(profile),
                                  connect,
                                  static_cast<unsigned long>(runtime.rx_bytes),
                                  static_cast<unsigned long>(runtime.rx_packets),
                                  static_cast<unsigned long>(runtime.rx_errors));
                }
                uart_flush_input(kCn105Uart);
            }
        }
    }

    if (best_score == 0) {
        best.baud = 2400;
        best.profile = 1;
        best.connect = 0x5A;
    }
    reopenCn105Uart(best.rx_pin, best.tx_pin, best.baud, best.profile, parity, stop_bits);
    last_probe = best;
    return best;
}

void writeString(nvs_handle_t handle, const char* key, const char* value) {
    ESP_ERROR_CHECK(nvs_set_str(handle, key, value == nullptr ? "" : value));
}

void writeSettingsToNvs(const InstallerSettings& s) {
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(kDeviceCfgNamespace, NVS_READWRITE, &handle));
    writeString(handle, "device_name", s.device_name);
    writeString(handle, "wifi_ssid", s.wifi_ssid);
    writeString(handle, "wifi_pass", s.wifi_pass);
    writeString(handle, "hk_code", s.hk_code);
    writeString(handle, "hk_mfr", s.hk_mfr);
    writeString(handle, "hk_model", s.hk_model);
    writeString(handle, "hk_serial", s.hk_serial);
    writeString(handle, "hk_setupid", s.hk_setupid);
    ESP_ERROR_CHECK(nvs_set_u8(handle, "use_real", s.use_real ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "led_on", s.led_on ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_i32(handle, "led_pin", s.led_pin));
    ESP_ERROR_CHECK(nvs_set_i32(handle, "rx_pin", s.rx_pin));
    ESP_ERROR_CHECK(nvs_set_i32(handle, "tx_pin", s.tx_pin));
    ESP_ERROR_CHECK(nvs_set_i32(handle, "baud", s.baud));
    ESP_ERROR_CHECK(nvs_set_i32(handle, "data_bits", s.data_bits));
    char parity_value[2] = {s.parity, '\0'};
    writeString(handle, "parity", parity_value);
    ESP_ERROR_CHECK(nvs_set_i32(handle, "stop_bits", s.stop_bits));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "rx_pull", s.rx_pull ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "tx_od", s.tx_od ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_u32(handle, "poll_on", s.poll_on));
    ESP_ERROR_CHECK(nvs_set_u32(handle, "poll_off", s.poll_off));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "log_level", s.log_level));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

const char* logLevelName(uint8_t level) {
    switch (level) {
        case ESP_LOG_ERROR:
            return "error";
        case ESP_LOG_WARN:
            return "warn";
        case ESP_LOG_INFO:
            return "info";
        case ESP_LOG_DEBUG:
            return "debug";
        case ESP_LOG_VERBOSE:
            return "verbose";
        default:
            return "none";
    }
}

bool parseLogLevel(const char* value, uint8_t* out) {
    if (std::strcmp(value, "none") == 0 || std::strcmp(value, "0") == 0) {
        *out = ESP_LOG_NONE;
        return true;
    }
    if (std::strcmp(value, "error") == 0 || std::strcmp(value, "1") == 0) {
        *out = ESP_LOG_ERROR;
        return true;
    }
    if (std::strcmp(value, "warn") == 0 || std::strcmp(value, "warning") == 0 || std::strcmp(value, "2") == 0) {
        *out = ESP_LOG_WARN;
        return true;
    }
    if (std::strcmp(value, "info") == 0 || std::strcmp(value, "3") == 0) {
        *out = ESP_LOG_INFO;
        return true;
    }
    if (std::strcmp(value, "debug") == 0 || std::strcmp(value, "4") == 0) {
        *out = ESP_LOG_DEBUG;
        return true;
    }
    if (std::strcmp(value, "verbose") == 0 || std::strcmp(value, "5") == 0) {
        *out = ESP_LOG_VERBOSE;
        return true;
    }
    return false;
}

bool parseSettingsFromBody(const char* body, InstallerSettings& s, char* error, size_t error_len) {
    char value[192] = {};
    if (formValue(body, "device_name", value, sizeof(value))) copyString(s.device_name, sizeof(s.device_name), value);
    if (formValue(body, "wifi_ssid", value, sizeof(value))) copyString(s.wifi_ssid, sizeof(s.wifi_ssid), value);
    if (formValue(body, "wifi_pass", value, sizeof(value))) copyString(s.wifi_pass, sizeof(s.wifi_pass), value);
    if (formValue(body, "hk_code", value, sizeof(value))) {
        if (!normalizeHomeKitCode(value, s.hk_code, sizeof(s.hk_code))) {
            copyString(error, error_len, "invalid HomeKit setup code");
            return false;
        }
    }
    if (formValue(body, "hk_mfr", value, sizeof(value))) copyString(s.hk_mfr, sizeof(s.hk_mfr), value);
    if (formValue(body, "hk_model", value, sizeof(value))) copyString(s.hk_model, sizeof(s.hk_model), value);
    if (formValue(body, "hk_serial", value, sizeof(value))) copyString(s.hk_serial, sizeof(s.hk_serial), value);
    if (formValue(body, "hk_setupid", value, sizeof(value))) copyString(s.hk_setupid, sizeof(s.hk_setupid), value);
    if (formValue(body, "use_real", value, sizeof(value))) s.use_real = !falseLike(value);
    if (formValue(body, "led_on", value, sizeof(value))) s.led_on = !falseLike(value);
    if (formValue(body, "led_pin", value, sizeof(value))) s.led_pin = std::atoi(value);
    if (formValue(body, "rx_pin", value, sizeof(value))) s.rx_pin = std::atoi(value);
    if (formValue(body, "tx_pin", value, sizeof(value))) s.tx_pin = std::atoi(value);
    if (formValue(body, "baud", value, sizeof(value))) s.baud = std::atoi(value);
    if (formValue(body, "data_bits", value, sizeof(value))) s.data_bits = std::atoi(value);
    if (formValue(body, "parity", value, sizeof(value)) && value[0] != '\0') s.parity = value[0];
    if (s.parity >= 'a' && s.parity <= 'z') s.parity = static_cast<char>(s.parity - 'a' + 'A');
    if (formValue(body, "stop_bits", value, sizeof(value))) s.stop_bits = std::atoi(value);
    if (formValue(body, "rx_pull", value, sizeof(value))) s.rx_pull = !falseLike(value);
    if (formValue(body, "tx_od", value, sizeof(value))) s.tx_od = !falseLike(value);
    if (formValue(body, "poll_on", value, sizeof(value))) s.poll_on = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    if (formValue(body, "poll_off", value, sizeof(value))) s.poll_off = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    if (formValue(body, "log_level", value, sizeof(value)) && !parseLogLevel(value, &s.log_level)) {
        copyString(error, error_len, "invalid log level");
        return false;
    }

    if (s.device_name[0] == '\0' || s.wifi_ssid[0] == '\0') {
        copyString(error, error_len, "device name and WiFi SSID are required");
        return false;
    }
    if (!validSetupId(s.hk_setupid)) {
        copyString(error, error_len, "invalid HomeKit setup id");
        return false;
    }
    if (!validOutputGpio(s.led_pin) || !validGpio(s.rx_pin) || !validOutputGpio(s.tx_pin)) {
        copyString(error, error_len, "invalid GPIO pin");
        return false;
    }
    if (!validBaud(s.baud) || s.data_bits != 8 || !(s.parity == 'N' || s.parity == 'E' || s.parity == 'O') ||
        !(s.stop_bits == 1 || s.stop_bits == 2)) {
        copyString(error, error_len, "invalid CN105 serial settings");
        return false;
    }
    if (s.poll_on < 1000 || s.poll_off < 5000) {
        copyString(error, error_len, "poll intervals are too small");
        return false;
    }
    if (s.log_level > ESP_LOG_VERBOSE) {
        copyString(error, error_len, "invalid log level");
        return false;
    }
    return true;
}

std::string settingsJson(const InstallerSettings& s) {
    char body[1600] = {};
    std::snprintf(body,
                  sizeof(body),
                  "{\"device_name\":\"%s\",\"wifi_ssid\":\"%s\",\"wifi_pass\":\"%s\","
                  "\"hk_code\":\"%c%c%c%c-%c%c%c%c\",\"hk_mfr\":\"%s\",\"hk_model\":\"%s\","
                  "\"hk_serial\":\"%s\",\"hk_setupid\":\"%s\",\"use_real\":%s,\"led_on\":%s,"
                  "\"led_pin\":%d,\"rx_pin\":%d,\"tx_pin\":%d,\"baud\":%d,\"data_bits\":%d,"
                  "\"parity\":\"%c\",\"stop_bits\":%d,\"rx_pull\":%s,\"tx_od\":%s,"
                  "\"poll_on\":%lu,\"poll_off\":%lu,\"log_level\":\"%s\"}",
                  jsonEscape(s.device_name).c_str(),
                  jsonEscape(s.wifi_ssid).c_str(),
                  jsonEscape(s.wifi_pass).c_str(),
                  s.hk_code[0], s.hk_code[1], s.hk_code[2], s.hk_code[3],
                  s.hk_code[4], s.hk_code[5], s.hk_code[6], s.hk_code[7],
                  jsonEscape(s.hk_mfr).c_str(),
                  jsonEscape(s.hk_model).c_str(),
                  jsonEscape(s.hk_serial).c_str(),
                  jsonEscape(s.hk_setupid).c_str(),
                  s.use_real ? "true" : "false",
                  s.led_on ? "true" : "false",
                  s.led_pin,
                  s.rx_pin,
                  s.tx_pin,
                  s.baud,
                  s.data_bits,
                  s.parity,
                  s.stop_bits,
                  s.rx_pull ? "true" : "false",
                  s.tx_od ? "true" : "false",
                  static_cast<unsigned long>(s.poll_on),
                  static_cast<unsigned long>(s.poll_off),
                  logLevelName(s.log_level));
    return body;
}

InstallerSettings defaultSettings() {
    InstallerSettings s{};
    if (last_probe.found) {
        s.rx_pin = last_probe.rx_pin;
        s.tx_pin = last_probe.tx_pin;
        s.baud = last_probe.baud;
        s.rx_pull = last_probe.profile >= 1;
        s.tx_od = last_probe.profile >= 2;
    }
    return s;
}

void setLedColor(int pin, uint8_t red, uint8_t green, uint8_t blue) {
    if (test_led != nullptr && test_led_pin != pin) {
        led_strip_del(test_led);
        test_led = nullptr;
    }
    if (test_led == nullptr) {
        led_strip_config_t strip_config = {};
        strip_config.strip_gpio_num = pin;
        strip_config.max_leds = 1;
        strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
        strip_config.led_model = LED_MODEL_WS2812;
        led_strip_rmt_config_t rmt_config = {};
        rmt_config.resolution_hz = 10 * 1000 * 1000;
        if (led_strip_new_rmt_device(&strip_config, &rmt_config, &test_led) != ESP_OK) {
            return;
        }
        test_led_pin = pin;
    }
    led_strip_set_pixel(test_led, 0, red, green, blue);
    led_strip_refresh(test_led);
}

void ledTestTask(void* arg) {
    const int pin = reinterpret_cast<intptr_t>(arg);
    setLedColor(pin, 255, 0, 80);
    vTaskDelay(pdMS_TO_TICKS(350));
    setLedColor(pin, 0, 255, 255);
    vTaskDelay(pdMS_TO_TICKS(350));
    setLedColor(pin, 255, 180, 0);
    vTaskDelay(pdMS_TO_TICKS(350));
    setLedColor(pin, 0, 0, 0);
    vTaskDelete(nullptr);
}

const char kIndexHtml[] = R"HTML(
<!doctype html><html lang="zh"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mitsubishi Installer</title>
<style>
:root{--bg:#080812;--card:#111126ee;--line:#3b1c45;--text:#f9eaff;--muted:#bba7c8;--hot:#ff4fd8;--cyan:#45f3ff;--ok:#69ff9b;--bad:#ff6b6b}
*{box-sizing:border-box}body{margin:0;font-family:ui-rounded,system-ui,-apple-system,BlinkMacSystemFont,sans-serif;color:var(--text);background:radial-gradient(circle at 10% 0,#40113d,transparent 32rem),radial-gradient(circle at 90% 12%,#0b6b79,transparent 28rem),var(--bg)}
main{max-width:1120px;margin:auto;padding:24px 16px 90px}h1{font-size:clamp(32px,6vw,68px);margin:12px 0 4px;letter-spacing:-.06em}.subtitle{color:var(--muted);font-size:15px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card{border:1px solid var(--line);background:linear-gradient(145deg,#151229ee,#0c0c19ee);border-radius:28px;padding:22px;margin:18px 0;box-shadow:0 0 36px #ff4fd81b}.card.locked{opacity:.52;filter:saturate(.55)}.step-note{color:var(--muted);margin:-4px 0 16px;font-size:14px}.field{display:flex;flex-direction:column;gap:7px;font-size:13px;color:var(--cyan);font-weight:800;letter-spacing:.04em}.field input,.field select{width:100%;border:1px solid #613069;background:#080916;color:var(--text);border-radius:16px;padding:13px 14px;font-size:16px}.inline-control{display:grid;grid-template-columns:1fr auto;gap:10px}.btns{display:flex;flex-wrap:wrap;gap:10px;margin-top:16px}button{border:1px solid #673070;background:#161326;color:var(--text);border-radius:999px;padding:12px 18px;font-weight:900;font-size:15px}button.primary{background:linear-gradient(135deg,var(--hot),var(--cyan));color:#090817;border:0}button.mini{padding:0 15px;border-radius:16px;white-space:nowrap}button:disabled,input:disabled,select:disabled{opacity:.45;cursor:not-allowed}.pill{display:inline-flex;border:1px solid #48304f;border-radius:999px;padding:6px 10px;margin:3px;color:var(--muted)}.status-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin-top:14px}.status-tile{border:1px solid #372142;background:#090916;border-radius:18px;padding:14px}.status-tile b{display:block;color:var(--cyan);font-size:12px;letter-spacing:.08em;text-transform:uppercase;margin-bottom:8px}.status-tile span{font-size:17px;font-weight:900}.json-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;margin-top:14px}.json-card h3{margin:0 0 8px;color:var(--hot);font-size:14px;letter-spacing:.05em}pre{white-space:pre-wrap;overflow:auto;background:#060711;border:1px solid #33213d;border-radius:18px;padding:14px;color:#c9f7ff;min-height:84px}.ok{color:var(--ok)}.bad{color:var(--bad)}@media(max-width:760px){main{padding:18px 12px 84px}.grid,.status-grid,.json-grid{grid-template-columns:1fr}.card{border-radius:22px;padding:18px}}
</style></head><body><main>
<h1>Installer / Probe</h1><div class="subtitle">1. 用 Espressif 手机 App 通过 BLE 配网。2. 打开本页检测 CN105。3. 保存配置到 NVS。4. 上传正式固件 OTA。</div>
<section class="card"><h2>连接状态</h2><div id="status">加载中...</div></section>
<section class="card" id="step1"><h2>第一步：CN105 硬件探测</h2><div class="step-note">先确认 RX/TX、波特率、电气参数、状态灯和正式固件 CN105 模式，然后显式保存一次到 NVS。</div><div class="grid">
<label class="field">CN105 RX GPIO<input id="rx_pin" type="number" value="26"></label><label class="field">CN105 TX GPIO<input id="tx_pin" type="number" value="32"></label>
<label class="field">串口校验<select id="parity"><option value="E">8E1</option><option value="N">8N1</option><option value="O">8O1</option></select></label><label class="field">停止位<select id="stop_bits"><option value="1">1</option><option value="2">2</option></select></label>
<label class="field">CN105 波特率<select id="baud"><option>2400</option><option>4800</option><option>9600</option></select></label><label class="field">CN105 RX 上拉<select id="rx_pull"><option value="1">开启</option><option value="0">关闭</option></select></label>
<label class="field">CN105 TX 开漏<select id="tx_od"><option value="0">关闭</option><option value="1">开启</option></select></label><label class="field">状态灯 GPIO<input id="led_pin" type="number" value="27"></label>
<label class="field">状态灯<select id="led_on"><option value="1">开启</option><option value="0">关闭</option></select></label><label class="field">正式固件 CN105 模式<select id="use_real"><option value="1">真实 CN105</option><option value="0">Mock</option></select></label>
</div><div class="btns"><button onclick="probe()">自动探测 CN105</button><button onclick="ledTest()">LED 五颜六色测试</button><button class="primary" onclick="saveStep1()">保存第一步并进入第二步</button></div><pre id="probe_out">还没有探测。</pre></section>
<section class="card locked" id="step2"><h2>第二步：确认并写入正式固件 NVS</h2><div class="step-note">第一步保存后才能编辑这里。确认 WiFi、HomeKit 和运行参数后，再保存一次完整配置到 NVS。</div><div class="grid">
<label class="field">设备名称<input id="device_name" value="Mitsubishi AC" autocomplete="off"></label><label class="field">WiFi SSID<input id="wifi_ssid" value="YOUR_WIFI_SSID" autocomplete="off" autocapitalize="none" spellcheck="false"></label><label class="field">WiFi 密码<input id="wifi_pass" type="text" value="YOUR_WIFI_PASSWORD" autocomplete="off" autocapitalize="none" spellcheck="false"></label>
<label class="field">HomeKit 配对码<div class="inline-control"><input id="hk_code" value="1111-2233" inputmode="numeric" autocomplete="off"><button class="mini" onclick="randomizeHomeKitCode()">随机</button></div></label><label class="field">HomeKit Setup ID<input id="hk_setupid" value="DKT1" maxlength="4"></label>
<label class="field">HomeKit 厂商<input id="hk_mfr" value="dkt smart home"></label><label class="field">HomeKit Model<input id="hk_model" value="Mitsubishi Heat Pump"></label><label class="field">HomeKit Serial<input id="hk_serial" value="DKT-MITSUBISHI-HOMEKIT"></label>
<label class="field">开机轮询 ms<input id="poll_on" type="number" value="15000"></label><label class="field">关机轮询 ms<input id="poll_off" type="number" value="60000"></label><label class="field">日志等级<select id="log_level"><option value="error" selected>Error</option><option value="warn">Warn</option><option value="info">Info</option><option value="debug">Debug</option><option value="verbose">Verbose</option></select></label>
</div><div class="btns"><button class="primary" onclick="saveStep2()">保存第二步并进入 OTA</button></div><pre id="nvs_out">请先完成第一步保存。</pre></section>
<section class="card locked" id="step3"><h2>第三步：上传正式固件 OTA</h2><div class="step-note">第二步保存后才能上传正式固件。</div><input id="fw" type="file" accept=".bin"><div class="btns"><button class="primary" onclick="upload()">上传正式固件</button><button onclick="applyOta()">重启应用 OTA</button></div><pre id="ota_out">请先完成第二步保存。</pre></section>
</main><script>
const $=id=>document.getElementById(id);
function val(id){return $(id).value}
function set(id,text){$(id).textContent=text}
function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function pretty(obj){return esc(JSON.stringify(obj||{},null,2))}
function setStepEnabled(step,enabled){const el=$('step'+step);el.classList.toggle('locked',!enabled);el.querySelectorAll('input,select,button').forEach(x=>x.disabled=!enabled)}
function initSteps(){setStepEnabled(2,false);setStepEnabled(3,false)}
function randomHomeKitCode(){const b=new Uint8Array(8);crypto.getRandomValues(b);const d=[...b].map(x=>String(x%10));return d.slice(0,4).join('')+'-'+d.slice(4).join('')}
function randomizeHomeKitCode(){$('hk_code').value=randomHomeKitCode()}
function params(){const p=new URLSearchParams();['device_name','wifi_ssid','wifi_pass','hk_code','hk_setupid','hk_mfr','hk_model','hk_serial','rx_pin','tx_pin','led_pin','baud','parity','stop_bits','rx_pull','tx_od','led_on','use_real','poll_on','poll_off','log_level'].forEach(id=>p.set(id,val(id)));p.set('data_bits','8');return p}
async function load(){const j=await (await fetch('/api/status')).json();$('status').innerHTML=`<div class="status-grid"><div class="status-tile"><b>WiFi</b><span class="${j.wifi.connected?'ok':'bad'}">${j.wifi.connected?'已连接':'未连接'}</span><div>${esc(j.wifi.ssid||'--')}</div><div>${esc(j.wifi.ip||'--')}</div></div><div class="status-tile"><b>BLE 配网名</b><span>${esc(j.provisioning.service_name)}</span></div><div class="status-tile"><b>Security</b><span>${esc(j.provisioning.security)}</span><div>PoP: ${esc(j.provisioning.pop)}</div></div><div class="status-tile"><b>探测结果</b><span class="${j.probe&&j.probe.found?'ok':'bad'}">${j.probe&&j.probe.found?'已找到':'未找到'}</span><div>${esc((j.probe&&j.probe.summary)||'Not run yet')}</div></div></div><div class="json-grid"><div class="json-card"><h3>NVS 默认值</h3><pre>${pretty(j.defaults)}</pre></div><div class="json-card"><h3>上次 CN105 探测</h3><pre>${pretty(j.probe)}</pre></div></div>`;for(const [k,v] of Object.entries(j.defaults)){if($(k))$(k).value=v}if(j.defaults){$('use_real').value=j.defaults.use_real?'1':'0';$('led_on').value=j.defaults.led_on?'1':'0';$('rx_pull').value=j.defaults.rx_pull?'1':'0';$('tx_od').value=j.defaults.tx_od?'1':'0'}if($('hk_code').value==='1111-2233')randomizeHomeKitCode();if(j.probe&&j.probe.found){$('rx_pin').value=j.probe.rx_pin;$('tx_pin').value=j.probe.tx_pin;$('baud').value=j.probe.baud;$('rx_pull').value=j.probe.profile>=1?'1':'0';$('tx_od').value=j.probe.profile>=2?'1':'0'}}
async function probe(){set('probe_out','探测中，大概几十秒...');const p=new URLSearchParams({rx_pin:val('rx_pin'),tx_pin:val('tx_pin'),parity:val('parity'),stop_bits:val('stop_bits')});const j=await (await fetch('/api/probe',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p})).json();set('probe_out',JSON.stringify(j,null,2));if(j.ok&&j.result){$('baud').value=j.result.baud;$('rx_pull').value=j.result.profile>=1?'1':'0';$('tx_od').value=j.result.profile>=2?'1':'0'}}
async function ledTest(){const j=await (await fetch('/api/led-test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({led_pin:val('led_pin')})})).json();set('probe_out',JSON.stringify(j,null,2))}
async function writeSettings(outId,pending){set(outId,pending);try{const j=await (await fetch('/api/write-settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params()})).json();set(outId,JSON.stringify(j,null,2));return !!j.ok}catch(e){set(outId,'保存失败: '+e);return false}}
async function saveStep1(){if(await writeSettings('probe_out','正在保存第一步硬件配置到 NVS...')){setStepEnabled(2,true);set('nvs_out','第一步已保存。请确认第二步配置，然后再次保存到 NVS。');$('step2').scrollIntoView({behavior:'smooth',block:'start'})}}
async function saveStep2(){if(await writeSettings('nvs_out','正在保存第二步完整配置到 NVS...')){setStepEnabled(3,true);set('ota_out','第二步已保存。现在可以上传 build/mitsubishi_heatpump_homekit.bin。');$('step3').scrollIntoView({behavior:'smooth',block:'start'})}}
async function upload(){const f=$('fw').files[0];if(!f){set('ota_out','请选择 .bin 文件');return}set('ota_out','上传中...');const j=await (await fetch('/api/ota/upload',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f})).json();set('ota_out',JSON.stringify(j,null,2))}
async function applyOta(){set('ota_out','正在重启，5秒后跳转到正式固件 WebUI (:8080)...');fetch('/api/ota/apply',{method:'POST'}).catch(()=>{});setTimeout(()=>location.href=`http://${location.hostname}:8080/`,5000)}
initSteps();
load().catch(e=>set('status','加载失败: '+e));
</script></body></html>
)HTML";

esp_err_t indexHandler(httpd_req_t* req) {
    return sendText(req, "text/html; charset=utf-8", kIndexHtml);
}

esp_err_t statusHandler(httpd_req_t* req) {
    InstallerSettings settings = defaultSettings();
    std::string body = "{\"ok\":true,\"wifi\":{\"connected\":";
    body += wifi_connected ? "true" : "false";
    body += ",\"ssid\":\"" + jsonEscape(wifi_ssid) + "\",\"ip\":\"" + jsonEscape(wifi_ip) + "\"}";
    body += ",\"provisioning\":{\"service_name\":\"";
    char service_name[32] = {};
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    std::snprintf(service_name, sizeof(service_name), "PROV_MITSUBISHI_%02X%02X%02X", mac[3], mac[4], mac[5]);
    body += service_name;
    body += "\",\"security\":\"Security 1\",\"pop\":\"";
    body += kProvisioningPop;
    body += "\"},\"defaults\":";
    body += settingsJson(settings);
    body += ",\"probe\":{\"ran\":";
    body += last_probe.ran ? "true" : "false";
    body += ",\"found\":";
    body += last_probe.found ? "true" : "false";
    body += ",\"rx_pin\":" + std::to_string(last_probe.rx_pin);
    body += ",\"tx_pin\":" + std::to_string(last_probe.tx_pin);
    body += ",\"baud\":" + std::to_string(last_probe.baud);
    body += ",\"profile\":" + std::to_string(last_probe.profile);
    body += ",\"summary\":\"" + jsonEscape(last_probe.summary) + "\"}}";
    return sendText(req, "application/json", body.c_str());
}

esp_err_t probeHandler(httpd_req_t* req) {
    char body[256] = {};
    if (!readBody(req, body, sizeof(body))) {
        return sendJsonError(req, "failed to read body");
    }
    char value[32] = {};
    int rx_pin = 26;
    int tx_pin = 32;
    char parity = 'E';
    int stop_bits = 1;
    if (formValue(body, "rx_pin", value, sizeof(value))) rx_pin = std::atoi(value);
    if (formValue(body, "tx_pin", value, sizeof(value))) tx_pin = std::atoi(value);
    if (formValue(body, "parity", value, sizeof(value)) && value[0] != '\0') parity = value[0];
    if (parity >= 'a' && parity <= 'z') parity = static_cast<char>(parity - 'a' + 'A');
    if (formValue(body, "stop_bits", value, sizeof(value))) stop_bits = std::atoi(value);
    if (!validGpio(rx_pin) || !validOutputGpio(tx_pin)) {
        return sendJsonError(req, "invalid GPIO pin");
    }
    ProbeResult result = runProbe(rx_pin, tx_pin, parity, stop_bits);
    char out[512] = {};
    std::snprintf(out,
                  sizeof(out),
                  "{\"ok\":true,\"result\":{\"found\":%s,\"rx_pin\":%d,\"tx_pin\":%d,\"baud\":%d,\"profile\":%u,\"profile_name\":\"%s\",\"connect\":\"0x%02X\",\"rx_bytes\":%lu,\"rx_packets\":%lu,\"rx_errors\":%lu,\"summary\":\"%s\"}}",
                  result.found ? "true" : "false",
                  result.rx_pin,
                  result.tx_pin,
                  result.baud,
                  static_cast<unsigned>(result.profile),
                  profileName(result.profile),
                  result.connect,
                  static_cast<unsigned long>(result.rx_bytes),
                  static_cast<unsigned long>(result.rx_packets),
                  static_cast<unsigned long>(result.rx_errors),
                  jsonEscape(result.summary).c_str());
    return sendText(req, "application/json", out);
}

esp_err_t writeSettingsHandler(httpd_req_t* req) {
    char body[2048] = {};
    if (!readBody(req, body, sizeof(body))) {
        return sendJsonError(req, "failed to read body");
    }
    InstallerSettings settings{};
    char error[128] = {};
    if (!parseSettingsFromBody(body, settings, error, sizeof(error))) {
        return sendJsonError(req, error);
    }
    writeSettingsToNvs(settings);
    std::string response = "{\"ok\":true,\"message\":\"device_cfg NVS saved\",\"settings\":";
    response += settingsJson(settings);
    response += "}";
    return sendText(req, "application/json", response.c_str());
}

esp_err_t ledTestHandler(httpd_req_t* req) {
    char body[128] = {};
    if (!readBody(req, body, sizeof(body))) {
        return sendJsonError(req, "failed to read body");
    }
    char value[32] = {};
    int pin = 27;
    if (formValue(body, "led_pin", value, sizeof(value))) pin = std::atoi(value);
    if (!validOutputGpio(pin)) {
        return sendJsonError(req, "invalid LED GPIO");
    }
    xTaskCreate(ledTestTask, "led_test", 3072, reinterpret_cast<void*>(static_cast<intptr_t>(pin)), 3, nullptr);
    return sendText(req, "application/json", "{\"ok\":true,\"message\":\"LED test started\"}");
}

esp_err_t otaUploadHandler(httpd_req_t* req) {
    if (req->content_len <= 0) {
        return sendJsonError(req, "empty upload");
    }
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);
    if (update == nullptr) {
        return sendJsonError(req, "no OTA partition");
    }
    if (running != nullptr && update->address == running->address) {
        return sendJsonError(req, "update partition matches running partition");
    }
    if (static_cast<size_t>(req->content_len) > update->size) {
        return sendJsonError(req, "firmware larger than OTA partition");
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(update, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        return sendJsonError(req, esp_err_to_name(err));
    }
    uint8_t buffer[1024] = {};
    int remaining = req->content_len;
    size_t written = 0;
    while (remaining > 0) {
        const int recv = httpd_req_recv(req, reinterpret_cast<char*>(buffer), std::min<int>(remaining, sizeof(buffer)));
        if (recv <= 0) {
            esp_ota_abort(handle);
            return sendJsonError(req, "upload receive failed");
        }
        err = esp_ota_write(handle, buffer, recv);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            return sendJsonError(req, esp_err_to_name(err));
        }
        written += static_cast<size_t>(recv);
        remaining -= recv;
    }
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        return sendJsonError(req, esp_err_to_name(err));
    }
    pending_ota_partition = update;
    char response[256] = {};
    std::snprintf(response,
                  sizeof(response),
                  "{\"ok\":true,\"bytes\":%u,\"partition\":\"%s\",\"address\":%u}",
                  static_cast<unsigned>(written),
                  update->label,
                  static_cast<unsigned>(update->address));
    return sendText(req, "application/json", response);
}

esp_err_t otaApplyHandler(httpd_req_t* req) {
    if (pending_ota_partition == nullptr) {
        return sendJsonError(req, "no pending OTA upload");
    }
    ESP_RETURN_ON_ERROR(esp_ota_set_boot_partition(pending_ota_partition), TAG, "set boot partition failed");
    sendText(req, "application/json", "{\"ok\":true,\"message\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

void startWebServer() {
    if (server != nullptr) {
        return;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 12288;
    config.max_uri_handlers = 12;
    config.max_open_sockets = 4;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = indexHandler, .user_ctx = nullptr},
        {.uri = "/api/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = nullptr},
        {.uri = "/api/probe", .method = HTTP_POST, .handler = probeHandler, .user_ctx = nullptr},
        {.uri = "/api/write-settings", .method = HTTP_POST, .handler = writeSettingsHandler, .user_ctx = nullptr},
        {.uri = "/api/led-test", .method = HTTP_POST, .handler = ledTestHandler, .user_ctx = nullptr},
        {.uri = "/api/ota/upload", .method = HTTP_POST, .handler = otaUploadHandler, .user_ctx = nullptr},
        {.uri = "/api/ota/apply", .method = HTTP_POST, .handler = otaApplyHandler, .user_ctx = nullptr},
    };
    for (const auto& route : routes) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    }
    ESP_LOGI(TAG, "Installer WebUI ready: http://%s/", wifi_ip);
}

void eventHandler(void*, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_PROV_EVENT) {
        if (event_id == WIFI_PROV_CRED_RECV) {
            auto* cfg = static_cast<wifi_sta_config_t*>(event_data);
            copyString(wifi_ssid, sizeof(wifi_ssid), reinterpret_cast<const char*>(cfg->ssid));
            copyString(wifi_password, sizeof(wifi_password), reinterpret_cast<const char*>(cfg->password));
            ESP_LOGI(TAG, "Provisioning received SSID=%s", wifi_ssid);
        } else if (event_id == WIFI_PROV_CRED_SUCCESS) {
            ESP_LOGI(TAG, "Provisioning successful");
        } else if (event_id == WIFI_PROV_CRED_FAIL) {
            ESP_LOGW(TAG, "Provisioning failed");
        } else if (event_id == WIFI_PROV_END) {
            wifi_prov_mgr_deinit();
        }
        return;
    }
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_connected = false;
            copyString(wifi_ip, sizeof(wifi_ip), "0.0.0.0");
            ESP_LOGW(TAG, "WiFi disconnected; reconnecting");
            esp_wifi_connect();
        }
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        std::snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        wifi_config_t cfg = {};
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
            copyString(wifi_ssid, sizeof(wifi_ssid), reinterpret_cast<const char*>(cfg.sta.ssid));
            copyString(wifi_password, sizeof(wifi_password), reinterpret_cast<const char*>(cfg.sta.password));
        }
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi connected: ssid=%s ip=%s", wifi_ssid, wifi_ip);
        startWebServer();
    }
}

void serviceName(char* out, size_t out_len) {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    std::snprintf(out, out_len, "PROV_MITSUBISHI_%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void startWifiProvisioning() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, eventHandler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventHandler, nullptr));

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    wifi_prov_mgr_config_t prov_config = {};
    prov_config.scheme = wifi_prov_scheme_ble;
    prov_config.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    prov_config.app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

    // Installer firmware should always be discoverable from Espressif's BLE provisioning app,
    // even if this board has stale Wi-Fi provisioning data from an older test.
    wifi_prov_mgr_reset_provisioning();

    char name[32] = {};
    serviceName(name, sizeof(name));
    const wifi_prov_security1_params_t* security_params = kProvisioningPop;
    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, security_params, name, nullptr));
    ESP_LOGI(TAG,
             "BLE provisioning started. Use Espressif app, service name: %s security=1 pop=%s",
             name,
             kProvisioningPop);
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Mitsubishi installer/probe starting");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    startWifiProvisioning();
    ESP_LOGI(TAG, "Waiting for WiFi before starting installer WebUI");
}
