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
#include "lwip/inet.h"
#include "lwip/sockets.h"
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
constexpr int kStableCn105DataBits = 8;
constexpr char kStableCn105Parity = 'E';
constexpr int kStableCn105StopBits = 1;
constexpr bool kStableCn105RxPull = true;
constexpr bool kStableCn105TxOpenDrain = false;
constexpr size_t kPacketLen = 22;
constexpr TickType_t kProbeWindowTicks = pdMS_TO_TICKS(900);
constexpr TickType_t kProbeSettleTicks = pdMS_TO_TICKS(120);
constexpr TickType_t kLoopDelayTicks = pdMS_TO_TICKS(10);
constexpr uint32_t kRxByteTimeoutMs = 140;
constexpr int WIFI_CONNECTED_BIT = BIT0;
constexpr const char* kDeviceCfgNamespace = "device_cfg";
constexpr const char* kProvisioningPop = "abcd1234";
constexpr int kAtomLiteStatusLedGpio = 27;
constexpr uint16_t kHttpPrimaryPort = 80;
constexpr uint16_t kDnsPort = 53;
constexpr char kApIp[] = "192.168.4.1";
constexpr char kCaptivePortalUrl[] = "http://192.168.4.1/";

EventGroupHandle_t wifi_event_group = nullptr;
esp_netif_t* ap_netif = nullptr;
httpd_handle_t server_80 = nullptr;
TaskHandle_t dns_task = nullptr;
char wifi_ip[16] = "0.0.0.0";
char wifi_ssid[33] = "";
char wifi_password[65] = "";
bool wifi_connected = false;
const esp_partition_t* pending_ota_partition = nullptr;
led_strip_handle_t test_led = nullptr;
int test_led_pin = -1;
char provisioning_service_name[40] = "";

enum class InstallerLedMode : uint8_t {
    Pairing,
    Provisioned,
};

volatile InstallerLedMode installer_led_mode = InstallerLedMode::Pairing;
TaskHandle_t installer_led_task = nullptr;

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
    int led_pin = 27;
    int rx_pin = 26;
    int tx_pin = 32;
    int baud = 2400;
    int data_bits = kStableCn105DataBits;
    char parity = kStableCn105Parity;
    int stop_bits = kStableCn105StopBits;
    bool rx_pull = kStableCn105RxPull;
    bool tx_od = kStableCn105TxOpenDrain;
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

void buildProvisioningServiceName(char* out, size_t out_len) {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    std::snprintf(out,
                  out_len,
                  "PROV_MITSUBISHI_%02X",
                  mac[0]);
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
        default: return "unknown";
    }
}

void configureCn105RxPullup(int rx_pin, uint8_t profile) {
    const gpio_num_t rx = static_cast<gpio_num_t>(rx_pin);
    const bool rx_pullup = profile >= 1;

    if (rx_pullup) {
        gpio_pullup_en(rx);
    } else {
        gpio_pullup_dis(rx);
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
    configureCn105RxPullup(rx_pin, profile);
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

ProbeResult runProbe(int rx_pin, int tx_pin) {
    ProbeResult best{};
    best.ran = true;
    best.rx_pin = rx_pin;
    best.tx_pin = tx_pin;
    copyString(best.summary, sizeof(best.summary), "No legal CN105 response detected");

    uint32_t best_score = 0;
    static constexpr uint8_t profiles[] = {0, 1};
    static constexpr uint8_t connects[] = {0x5A, 0x5B};

    for (int baud : kBaudOptions) {
        for (uint8_t profile : profiles) {
            if (reopenCn105Uart(rx_pin, tx_pin, baud, profile, kStableCn105Parity, kStableCn105StopBits) != ESP_OK) {
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
    reopenCn105Uart(best.rx_pin, best.tx_pin, best.baud, best.profile, kStableCn105Parity, kStableCn105StopBits);
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

void loadStringFromNvs(nvs_handle_t handle, const char* key, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    size_t len = out_len;
    const esp_err_t err = nvs_get_str(handle, key, out, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read NVS string %s: %s", key, esp_err_to_name(err));
    }
}

void loadI32FromNvs(nvs_handle_t handle, const char* key, int* out) {
    int32_t value = 0;
    const esp_err_t err = nvs_get_i32(handle, key, &value);
    if (err == ESP_OK) {
        *out = static_cast<int>(value);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read NVS i32 %s: %s", key, esp_err_to_name(err));
    }
}

void loadU32FromNvs(nvs_handle_t handle, const char* key, uint32_t* out) {
    uint32_t value = 0;
    const esp_err_t err = nvs_get_u32(handle, key, &value);
    if (err == ESP_OK) {
        *out = value;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read NVS u32 %s: %s", key, esp_err_to_name(err));
    }
}

void loadU8FromNvs(nvs_handle_t handle, const char* key, uint8_t* out) {
    uint8_t value = 0;
    const esp_err_t err = nvs_get_u8(handle, key, &value);
    if (err == ESP_OK) {
        *out = value;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read NVS u8 %s: %s", key, esp_err_to_name(err));
    }
}

void loadSettingsFromNvs(InstallerSettings& s) {
    nvs_handle_t handle = 0;
    const esp_err_t err = nvs_open(kDeviceCfgNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open device_cfg NVS: %s", esp_err_to_name(err));
        return;
    }

    loadStringFromNvs(handle, "device_name", s.device_name, sizeof(s.device_name));
    loadStringFromNvs(handle, "wifi_ssid", s.wifi_ssid, sizeof(s.wifi_ssid));
    loadStringFromNvs(handle, "wifi_pass", s.wifi_pass, sizeof(s.wifi_pass));
    loadStringFromNvs(handle, "hk_code", s.hk_code, sizeof(s.hk_code));
    loadStringFromNvs(handle, "hk_mfr", s.hk_mfr, sizeof(s.hk_mfr));
    loadStringFromNvs(handle, "hk_model", s.hk_model, sizeof(s.hk_model));
    loadStringFromNvs(handle, "hk_serial", s.hk_serial, sizeof(s.hk_serial));
    loadStringFromNvs(handle, "hk_setupid", s.hk_setupid, sizeof(s.hk_setupid));
    uint8_t use_real = s.use_real ? 1 : 0;
    loadU8FromNvs(handle, "use_real", &use_real);
    s.use_real = use_real != 0;
    loadI32FromNvs(handle, "led_pin", &s.led_pin);
    loadI32FromNvs(handle, "rx_pin", &s.rx_pin);
    loadI32FromNvs(handle, "tx_pin", &s.tx_pin);
    loadI32FromNvs(handle, "baud", &s.baud);
    loadI32FromNvs(handle, "data_bits", &s.data_bits);
    char parity_value[4] = {};
    loadStringFromNvs(handle, "parity", parity_value, sizeof(parity_value));
    if (parity_value[0] != '\0') {
        s.parity = parity_value[0];
    }
    loadI32FromNvs(handle, "stop_bits", &s.stop_bits);
    uint8_t rx_pull = s.rx_pull ? 1 : 0;
    loadU8FromNvs(handle, "rx_pull", &rx_pull);
    s.rx_pull = rx_pull != 0;
    uint8_t tx_od = s.tx_od ? 1 : 0;
    loadU8FromNvs(handle, "tx_od", &tx_od);
    s.tx_od = tx_od != 0;
    loadU32FromNvs(handle, "poll_on", &s.poll_on);
    loadU32FromNvs(handle, "poll_off", &s.poll_off);
    loadU8FromNvs(handle, "log_level", &s.log_level);
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
    if (formValue(body, "led_pin", value, sizeof(value))) s.led_pin = std::atoi(value);
    if (formValue(body, "rx_pin", value, sizeof(value))) s.rx_pin = std::atoi(value);
    if (formValue(body, "tx_pin", value, sizeof(value))) s.tx_pin = std::atoi(value);
    if (formValue(body, "baud", value, sizeof(value))) s.baud = std::atoi(value);
    if (formValue(body, "data_bits", value, sizeof(value))) s.data_bits = std::atoi(value);
    if (formValue(body, "parity", value, sizeof(value)) && value[0] != '\0') s.parity = value[0];
    if (formValue(body, "stop_bits", value, sizeof(value))) s.stop_bits = std::atoi(value);
    if (formValue(body, "rx_pull", value, sizeof(value))) s.rx_pull = !falseLike(value);
    if (formValue(body, "tx_od", value, sizeof(value))) s.tx_od = !falseLike(value);
    if (formValue(body, "poll_on", value, sizeof(value))) s.poll_on = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    if (formValue(body, "poll_off", value, sizeof(value))) s.poll_off = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    if (formValue(body, "log_level", value, sizeof(value)) && !parseLogLevel(value, &s.log_level)) {
        copyString(error, error_len, "invalid log level");
        return false;
    }

    if (s.device_name[0] == '\0') {
        copyString(error, error_len, "device name is required");
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
    if (!validBaud(s.baud)) {
        copyString(error, error_len, "invalid CN105 baud rate");
        return false;
    }
    if (s.data_bits != 8 || (s.parity != 'E' && s.parity != 'N' && s.parity != 'O') ||
        (s.stop_bits != 1 && s.stop_bits != 2)) {
        copyString(error, error_len, "invalid CN105 serial format");
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
                  "\"hk_serial\":\"%s\",\"hk_setupid\":\"%s\",\"use_real\":%s,"
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
    loadSettingsFromNvs(s);
    if (wifi_ssid[0] != '\0') {
        copyString(s.wifi_ssid, sizeof(s.wifi_ssid), wifi_ssid);
    }
    if (wifi_password[0] != '\0') {
        copyString(s.wifi_pass, sizeof(s.wifi_pass), wifi_password);
    }
    if (last_probe.found) {
        s.rx_pin = last_probe.rx_pin;
        s.tx_pin = last_probe.tx_pin;
        s.baud = last_probe.baud;
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

void setInstallerLedMode(InstallerLedMode mode) {
    installer_led_mode = mode;
}

void installerLedTask(void*) {
    bool blink_on = false;
    while (true) {
        if (installer_led_mode == InstallerLedMode::Provisioned) {
            setLedColor(kAtomLiteStatusLedGpio, 0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        blink_on = !blink_on;
        setLedColor(kAtomLiteStatusLedGpio, 0, 0, blink_on ? 255 : 0);
        vTaskDelay(pdMS_TO_TICKS(450));
    }
}

void startInstallerStatusLed() {
    if (installer_led_task != nullptr) {
        return;
    }
    setInstallerLedMode(InstallerLedMode::Pairing);
    xTaskCreate(installerLedTask, "installer_led", 3072, nullptr, 3, &installer_led_task);
}

const char kIndexHtml[] = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mitsubishi Installer</title>
<style>
:root{--bg:#080812;--card:#111126ee;--line:#3b1c45;--text:#f9eaff;--muted:#bba7c8;--hot:#ff4fd8;--cyan:#45f3ff;--ok:#69ff9b;--bad:#ff6b6b}
*{box-sizing:border-box}body{margin:0;font-family:ui-rounded,system-ui,-apple-system,BlinkMacSystemFont,sans-serif;color:var(--text);background:radial-gradient(circle at 10% 0,#40113d,transparent 32rem),radial-gradient(circle at 90% 12%,#0b6b79,transparent 28rem),var(--bg)}
main{max-width:1120px;margin:auto;padding:24px 16px 90px}h1{font-size:clamp(32px,6vw,68px);margin:12px 0 4px;letter-spacing:-.06em}.subtitle{color:var(--muted);font-size:15px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card{border:1px solid var(--line);background:linear-gradient(145deg,#151229ee,#0c0c19ee);border-radius:28px;padding:22px;margin:18px 0;box-shadow:0 0 36px #ff4fd81b}.card.locked{opacity:.52;filter:saturate(.55)}.step-note{color:var(--muted);margin:-4px 0 16px;font-size:14px}.field{display:flex;flex-direction:column;gap:7px;font-size:13px;color:var(--cyan);font-weight:800;letter-spacing:.04em}.field input,.field select{width:100%;border:1px solid #613069;background:#080916;color:var(--text);border-radius:16px;padding:13px 14px;font-size:16px}.field-label{position:relative;display:inline-flex;align-items:center;gap:7px;width:fit-content;max-width:100%}.help-dot{display:inline-flex;align-items:center;justify-content:center;width:18px;height:18px;border:1px solid rgba(69,243,255,.34);border-radius:999px;background:rgba(69,243,255,.10);color:var(--text);font-size:11px;line-height:1;cursor:help}.field-tip{position:absolute;left:0;top:calc(100% + 8px);z-index:20;display:none;width:min(300px,calc(100vw - 56px));border:1px solid rgba(255,79,216,.30);border-radius:14px;background:linear-gradient(180deg,rgba(10,15,31,.98),rgba(6,10,22,.98));color:var(--text);padding:10px 12px;font-size:12px;font-style:normal;font-weight:700;line-height:1.45;letter-spacing:0;box-shadow:0 18px 44px rgba(0,0,0,.46),0 0 24px rgba(255,79,216,.13)}.field-label:hover .field-tip,.field-label:focus-within .field-tip{display:block}.config-summary-button{width:100%;min-height:51px;border:1px solid #613069;background:#080916;color:var(--text);border-radius:16px;padding:13px 14px;font-size:16px;text-align:left;letter-spacing:.01em}.config-summary-button:hover{border-color:var(--hot);box-shadow:0 0 18px #ff4fd833}.inline-control{display:grid;grid-template-columns:1fr auto;gap:10px}.btns{display:flex;flex-wrap:wrap;gap:10px;margin-top:16px}button{border:1px solid #673070;background:#161326;color:var(--text);border-radius:999px;padding:12px 18px;font-weight:900;font-size:15px;cursor:pointer}button.primary{background:linear-gradient(135deg,var(--hot),var(--cyan));color:#090817;border:0}button.mini{padding:0 15px;border-radius:16px;white-space:nowrap}button:disabled,input:disabled,select:disabled{opacity:.45;cursor:not-allowed}.pill{display:inline-flex;border:1px solid #48304f;border-radius:999px;padding:6px 10px;margin:3px;color:var(--muted)}.status-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin-top:14px}.status-tile{border:1px solid #372142;background:#090916;border-radius:18px;padding:14px}.status-tile b{display:block;color:var(--cyan);font-size:12px;letter-spacing:.08em;text-transform:uppercase;margin-bottom:8px}.status-tile span{font-size:17px;font-weight:900}.progress{display:none;width:100%;margin-top:14px}.modal{position:fixed;inset:0;display:none;align-items:center;justify-content:center;padding:18px;background:rgba(3,6,15,.78);backdrop-filter:blur(10px);z-index:50}.modal.open{display:flex}.modal-panel{width:min(640px,calc(100vw - 24px));max-height:calc(100vh - 28px);overflow:auto;border:1px solid #7a2c82;border-radius:26px;background:linear-gradient(145deg,#151229,#090916);padding:22px;box-shadow:0 24px 80px rgba(0,0,0,.58),0 0 42px #ff4fd822}.modal-panel h2{margin-top:0}.danger{border:1px solid rgba(255,107,107,.58);background:rgba(255,107,107,.12);border-radius:18px;padding:14px;margin:14px 0;color:#ffe8ee;font-weight:800}pre{white-space:pre-wrap;overflow:auto;background:#060711;border:1px solid #33213d;border-radius:18px;padding:14px;color:#c9f7ff;min-height:84px}.ok{color:var(--ok)}.bad{color:var(--bad)}@media(max-width:760px){main{padding:18px 12px 84px}.grid,.status-grid{grid-template-columns:1fr}.card{border-radius:22px;padding:18px}}
</style></head><body><main>
<h1>Installer / Probe</h1><div class="subtitle">Provision WiFi over BLE, save production device settings, optionally test CN105, then upload the production firmware over OTA.</div>
<section class="card"><h2>Connection Status</h2><div id="status">Loading...</div></section>
<section class="card" id="step1"><h2>Step 1: Device Settings</h2><div class="step-note">These values are written to the same NVS namespace used by the production firmware. Saving here does not reboot the installer.</div><div class="grid">
<label class="field">Device Name<input id="device_name" value="Mitsubishi AC" autocomplete="off"></label><label class="field">WiFi SSID<input id="wifi_ssid" value="YOUR_WIFI_SSID" autocomplete="off" autocapitalize="none" spellcheck="false"></label><label class="field">WiFi Password<input id="wifi_pass" type="text" value="YOUR_WIFI_PASSWORD" autocomplete="off" autocapitalize="none" spellcheck="false"></label>
<label class="field">HomeKit Pairing Code<div class="inline-control"><input id="hk_code" value="1111-2233" inputmode="numeric" autocomplete="off"><button class="mini" onclick="randomizeHomeKitCode()" type="button">Random</button></div></label><label class="field">HomeKit Setup ID<input id="hk_setupid" value="DKT1" maxlength="4"></label>
<label class="field">HomeKit Manufacturer<input id="hk_mfr" value="dkt smart home"></label><label class="field">HomeKit Model<input id="hk_model" value="Mitsubishi Heat Pump"></label><label class="field">HomeKit Serial<input id="hk_serial" value="DKT-MITSUBISHI-HOMEKIT"></label>
<label class="field">Status LED GPIO<input id="led_pin" type="number" value="27"></label><label class="field">Log Level<select id="log_level"><option value="error" selected>Error</option><option value="warn">Warn</option><option value="info">Info</option><option value="debug">Debug</option><option value="verbose">Verbose</option></select></label>
<label class="field"><span class="field-label">On Polling (ms)<b class="help-dot" tabindex="0" aria-label="How often production firmware queries CN105 while the AC is on. Lower values sync faster but communicate more often.">?</b><em class="field-tip">How often production firmware queries CN105 while the AC is on. Lower values sync faster but communicate more often.</em></span><input id="poll_on" type="number" min="1000" step="1000" value="15000"></label><label class="field"><span class="field-label">Off Polling (ms)<b class="help-dot" tabindex="0" aria-label="How often production firmware queries CN105 while the AC is off. This can usually be longer to avoid unnecessary traffic.">?</b><em class="field-tip">How often production firmware queries CN105 while the AC is off. This can usually be longer to avoid unnecessary traffic.</em></span><input id="poll_off" type="number" min="5000" step="1000" value="60000"></label>
<label class="field">CN105 Mode<select id="use_real"><option value="1">Real CN105</option><option value="0">Mock</option></select></label>
<label class="field">CN105 Advanced Settings<button class="config-summary-button" id="cn105_advanced_btn" type="button">8E1 2400 RX G26 TX G32</button></label>
</div><div class="btns"><button onclick="ledTest()" type="button">LED Color Test</button><button class="primary" onclick="saveStep1()" type="button">Save and Continue</button></div><pre id="step1_out">Save Step 1 before continuing.</pre></section>
<section class="card locked" id="step2"><h2>Step 2: Optional CN105 Smoke Test</h2><div class="step-note">If the AC is connected, run a safe CN105 CONNECT/INFO probe using the saved pins and baud rate. You may also skip this step.</div><div class="btns"><button onclick="backToStep1()" type="button">Back to Step 1</button><button onclick="probe()" type="button">Run CN105 Test</button><button class="primary" onclick="continueToOta()" type="button">Continue to OTA</button></div><pre id="probe_out">Optional test has not run.</pre></section>
<section class="card locked" id="step3"><h2>Step 3: OTA Update</h2><div class="step-note">Select the production app firmware at address <code>0x20000</code> from <code>firmware_exports/&lt;version&gt;/</code>. Upload starts automatically after selection.</div><input id="fw" type="file" accept=".bin,application/octet-stream"><progress id="ota_progress" class="progress" value="0" max="100"></progress><pre id="ota_out">Save Step 1, then continue from Step 2.</pre></section>
</main>
<div id="ota_modal" class="modal" aria-hidden="true"><div class="modal-panel"><h2>Confirm OTA Update</h2><div class="subtitle">Firmware upload is complete. Confirm to reboot and boot the production firmware.</div><div class="danger">Canceling will not switch firmware. To change your mind, select and upload the firmware file again.</div><pre id="ota_confirm_body">--</pre><div class="btns"><button class="primary" onclick="applyOta()" type="button">Confirm Reboot and Apply</button><button onclick="cancelOta()" type="button">Cancel</button></div></div></div>
<div id="cn105_modal" class="modal" aria-hidden="true"><div class="modal-panel"><h2>CN105 Advanced Settings</h2><div class="subtitle">These values affect ESP32-to-CN105 serial communication. Bad GPIO, baud, or electrical settings can make the AC stop responding. Confirm only updates the local Step 1 draft; click Save and Continue afterward to write NVS.</div><div class="danger">Dangerous advanced settings. If CN105 communication currently works, do not change these values unless you are actively debugging hardware or wiring.</div><div class="grid">
<label class="field">CN105 RX GPIO<input id="rx_pin" type="number" min="0" max="39" step="1" value="26"></label><label class="field">CN105 TX GPIO<input id="tx_pin" type="number" min="0" max="33" step="1" value="32"></label>
<label class="field">CN105 Baud Rate<select id="baud"><option>2400</option><option>4800</option><option>9600</option></select></label><label class="field">CN105 Data Bits<select id="data_bits"><option value="8">8</option></select></label>
<label class="field">CN105 Parity<select id="parity"><option value="E">Even</option><option value="N">None</option><option value="O">Odd</option></select></label><label class="field">CN105 Stop Bits<select id="stop_bits"><option value="1">1</option><option value="2">2</option></select></label>
<label class="field">CN105 RX Pullup<select id="rx_pull"><option value="1">On</option><option value="0">Off</option></select></label><label class="field">CN105 TX Open Drain<select id="tx_od"><option value="0">Off</option><option value="1">On</option></select></label>
</div><div class="btns"><button class="primary" onclick="closeCn105Modal(true,event)" type="button">Confirm</button><button onclick="closeCn105Modal(false,event)" type="button">Cancel</button></div></div></div>
<script>
const $=id=>document.getElementById(id);
function val(id){return $(id).value}
function set(id,text){$(id).textContent=text}
function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function pretty(obj){return esc(JSON.stringify(obj||{},null,2))}
function setStepEnabled(step,enabled){const el=$('step'+step);el.classList.toggle('locked',!enabled);el.querySelectorAll('input,select,button').forEach(x=>x.disabled=!enabled)}
function initSteps(){setStepEnabled(2,false);setStepEnabled(3,false)}
function placeholderWifi(value,placeholder){return !value||value===placeholder}
function randomHomeKitCode(){const b=new Uint8Array(8);crypto.getRandomValues(b);const d=[...b].map(x=>String(x%10));return d.slice(0,4).join('')+'-'+d.slice(4).join('')}
function randomizeHomeKitCode(){$('hk_code').value=randomHomeKitCode()}
const cn105Ids=['rx_pin','tx_pin','baud','data_bits','parity','stop_bits','rx_pull','tx_od'];
let cn105Snapshot=null;
function cn105Summary(){return `${val('data_bits')}${val('parity')}${val('stop_bits')} ${val('baud')} RX G${val('rx_pin')} TX G${val('tx_pin')}`}
function updateCn105Summary(){$('cn105_advanced_btn').textContent=cn105Summary()}
function captureCn105(){const out={};cn105Ids.forEach(id=>out[id]=val(id));return out}
function restoreCn105(values){if(!values)return;for(const [id,value] of Object.entries(values)){if($(id))$(id).value=value}updateCn105Summary()}
function openCn105Modal(){cn105Snapshot=captureCn105();$('cn105_modal').classList.add('open');$('cn105_modal').setAttribute('aria-hidden','false');updateCn105Summary()}
function closeCn105Modal(keep,event){event?.preventDefault();if(!keep)restoreCn105(cn105Snapshot);cn105Snapshot=null;updateCn105Summary();$('cn105_modal').classList.remove('open');$('cn105_modal').setAttribute('aria-hidden','true')}
function params(){const p=new URLSearchParams();['device_name','wifi_ssid','wifi_pass','hk_code','hk_setupid','hk_mfr','hk_model','hk_serial','rx_pin','tx_pin','led_pin','baud','data_bits','parity','stop_bits','rx_pull','tx_od','use_real','poll_on','poll_off','log_level'].forEach(id=>p.set(id,val(id)));return p}
async function load(){const j=await (await fetch('/api/status')).json();$('status').innerHTML=`<div class="status-grid"><div class="status-tile"><b>WiFi STA</b><span class="${j.wifi.connected?'ok':'bad'}">${j.wifi.connected?'Connected':'Not Connected'}</span><div>${esc(j.wifi.ssid||'--')}</div><div>${esc(j.wifi.ip||'--')}</div></div><div class="status-tile"><b>Installer AP</b><span>${esc(j.wifi.ap_ssid)}</span><div>${esc(j.wifi.ap_ip)}</div></div><div class="status-tile"><b>BLE Service</b><span>${esc(j.provisioning.service_name)}</span></div><div class="status-tile"><b>Security</b><span>${esc(j.provisioning.security)}</span><div>PoP: ${esc(j.provisioning.pop)}</div></div></div>`;for(const [k,v] of Object.entries(j.defaults)){if($(k))$(k).value=v}if(j.defaults){$('use_real').value=j.defaults.use_real?'1':'0';$('rx_pull').value=j.defaults.rx_pull?'1':'0';$('tx_od').value=j.defaults.tx_od?'1':'0'}if($('hk_code').value==='1111-2233')randomizeHomeKitCode();if(j.probe&&j.probe.found){$('rx_pin').value=j.probe.rx_pin;$('tx_pin').value=j.probe.tx_pin;$('baud').value=j.probe.baud}updateCn105Summary()}
async function probe(){set('probe_out','Probing. This may take a few dozen seconds...');const p=new URLSearchParams({rx_pin:val('rx_pin'),tx_pin:val('tx_pin')});const j=await (await fetch('/api/probe',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p})).json();set('probe_out',JSON.stringify(j,null,2));if(j.ok&&j.result){$('baud').value=j.result.baud;updateCn105Summary()}}
async function ledTest(){const j=await (await fetch('/api/led-test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({led_pin:val('led_pin')})})).json();set('step1_out',JSON.stringify(j,null,2))}
async function writeSettings(outId,pending){set(outId,pending);try{const j=await (await fetch('/api/write-settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params()})).json();set(outId,JSON.stringify(j,null,2));return !!j.ok}catch(e){set(outId,'Save failed: '+e);return false}}
async function saveStep1(){const ssid=$('wifi_ssid').value.trim();const pass=$('wifi_pass').value.trim();if(placeholderWifi(ssid,'YOUR_WIFI_SSID')||placeholderWifi(pass,'YOUR_WIFI_PASSWORD')){set('step1_out','Enter a real WiFi SSID and password. Do not keep the placeholder values.');$('wifi_ssid').focus();return}if(await writeSettings('step1_out','Saving Step 1 settings to NVS...')){setStepEnabled(1,false);setStepEnabled(2,true);set('probe_out','Step 1 saved. Optional: run a CN105 smoke test, or continue to OTA.');$('step2').scrollIntoView({behavior:'smooth',block:'start'})}}
function backToStep1(){setStepEnabled(1,true);setStepEnabled(2,false);setStepEnabled(3,false);$('step1').scrollIntoView({behavior:'smooth',block:'start'})}
function continueToOta(){setStepEnabled(2,false);setStepEnabled(3,true);set('ota_out','Select the production app firmware. Upload starts automatically.');$('step3').scrollIntoView({behavior:'smooth',block:'start'})}
async function upload(){const f=$('fw').files[0];if(!f){set('ota_out','Select a .bin file');return}const name=(f.name||'').toLowerCase();if(!name.endsWith('.bin')){set('ota_out','Only .bin firmware files are allowed.');$('fw').value='';return}if(!name.endsWith('_0x20000.bin')){set('ota_out','Select the app firmware at address 0x20000.');$('fw').value='';return}const progress=$('ota_progress');progress.style.display='block';progress.value=0;set('ota_out','Uploading '+f.name+' ...');const xhr=new XMLHttpRequest();xhr.open('POST','/api/ota/upload');xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.upload.onprogress=e=>{if(e.lengthComputable)progress.value=Math.round((e.loaded/e.total)*100)};xhr.onload=()=>{let j={};try{j=JSON.parse(xhr.responseText||'{}')}catch(e){};if(xhr.status>=200&&xhr.status<300&&j.ok){progress.style.display='none';$('fw').value='';$('ota_confirm_body').textContent=JSON.stringify(j,null,2);$('ota_modal').classList.add('open');$('ota_modal').setAttribute('aria-hidden','false');set('ota_out','Upload complete. Confirm in the modal to reboot and apply.')}else{set('ota_out','OTA upload failed: '+(j.error||xhr.responseText||xhr.status));}};xhr.onerror=()=>set('ota_out','OTA upload failed: network error');xhr.send(f)}
function cancelOta(){$('ota_modal').classList.remove('open');$('ota_modal').setAttribute('aria-hidden','true');set('ota_out','Upload canceled. Select the firmware again if you want to apply it.')}
async function applyOta(){set('ota_out','Rebooting. Redirecting to the production WebUI (:8080) in 5 seconds...');$('ota_modal').classList.remove('open');fetch('/api/ota/apply',{method:'POST'}).catch(()=>{});setTimeout(()=>location.href=`http://${location.hostname}:8080/`,5000)}
$('cn105_advanced_btn').addEventListener('click',openCn105Modal);
cn105Ids.forEach(id=>{$(id).addEventListener('input',updateCn105Summary);$(id).addEventListener('change',updateCn105Summary)});
$('fw').addEventListener('change',upload);
initSteps();
load().catch(e=>set('status','Load failed: '+e));
</script></body></html>
)HTML";

esp_err_t indexHandler(httpd_req_t* req) {
    return sendText(req, "text/html; charset=utf-8", kIndexHtml);
}

esp_err_t statusHandler(httpd_req_t* req) {
    InstallerSettings settings = defaultSettings();
    std::string body = "{\"ok\":true,\"wifi\":{\"connected\":";
    body += wifi_connected ? "true" : "false";
    body += ",\"ssid\":\"" + jsonEscape(wifi_ssid) + "\",\"ip\":\"" + jsonEscape(wifi_ip) + "\"";
    body += ",\"ap_ssid\":\"" + jsonEscape(provisioning_service_name) + "\",\"ap_ip\":\"" + jsonEscape(kApIp) + "\"}";
    body += ",\"provisioning\":{\"service_name\":\"";
    body += provisioning_service_name;
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
    if (formValue(body, "rx_pin", value, sizeof(value))) rx_pin = std::atoi(value);
    if (formValue(body, "tx_pin", value, sizeof(value))) tx_pin = std::atoi(value);
    if (!validGpio(rx_pin) || !validOutputGpio(tx_pin)) {
        return sendJsonError(req, "invalid GPIO pin");
    }
    ProbeResult result = runProbe(rx_pin, tx_pin);
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
    const int pin = kAtomLiteStatusLedGpio;
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

void registerRoutes(httpd_handle_t handle) {
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = indexHandler, .user_ctx = nullptr},
        {.uri = "/api/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = nullptr},
        {.uri = "/api/probe", .method = HTTP_POST, .handler = probeHandler, .user_ctx = nullptr},
        {.uri = "/api/write-settings", .method = HTTP_POST, .handler = writeSettingsHandler, .user_ctx = nullptr},
        {.uri = "/api/led-test", .method = HTTP_POST, .handler = ledTestHandler, .user_ctx = nullptr},
        {.uri = "/api/ota/upload", .method = HTTP_POST, .handler = otaUploadHandler, .user_ctx = nullptr},
        {.uri = "/api/ota/apply", .method = HTTP_POST, .handler = otaApplyHandler, .user_ctx = nullptr},
        // Captive portal probes use many URLs. Serve the installer shell for all GET misses.
        {.uri = "/*", .method = HTTP_GET, .handler = indexHandler, .user_ctx = nullptr},
    };
    for (const auto& route : routes) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &route));
    }
}

void startWebServer() {
    if (server_80 != nullptr) {
        return;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kHttpPrimaryPort;
    config.stack_size = 12288;
    config.max_uri_handlers = 16;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&server_80, &config));
    registerRoutes(server_80);
    ESP_LOGI(TAG,
             "Installer WebUI ready on port %u: AP=http://%s:%u/ STA=http://%s:%u/",
             static_cast<unsigned>(config.server_port),
             kApIp,
             static_cast<unsigned>(config.server_port),
             wifi_ip,
             static_cast<unsigned>(config.server_port));
}

void captiveDnsTask(void*) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create captive DNS socket");
        dns_task = nullptr;
        vTaskDelete(nullptr);
    }

    sockaddr_in listen_addr = {};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(kDnsPort);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr*>(&listen_addr), sizeof(listen_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind captive DNS socket");
        closesocket(sock);
        dns_task = nullptr;
        vTaskDelete(nullptr);
    }

    uint8_t query[512] = {};
    uint8_t response[512] = {};
    const uint32_t ap_addr = inet_addr(kApIp);
    while (true) {
        sockaddr_in source_addr = {};
        socklen_t source_len = sizeof(source_addr);
        const int query_len = recvfrom(sock,
                                       query,
                                       sizeof(query),
                                       0,
                                       reinterpret_cast<sockaddr*>(&source_addr),
                                       &source_len);
        if (query_len < 12) {
            continue;
        }

        memcpy(response, query, query_len);
        response[2] = 0x81;  // response, recursion desired/available
        response[3] = 0x80;
        response[6] = 0x00;  // ANCOUNT = 1
        response[7] = 0x01;

        size_t out = static_cast<size_t>(query_len);
        if (out + 16 > sizeof(response)) {
            continue;
        }
        response[out++] = 0xC0;  // pointer to the original QNAME at offset 12
        response[out++] = 0x0C;
        response[out++] = 0x00;  // TYPE A
        response[out++] = 0x01;
        response[out++] = 0x00;  // CLASS IN
        response[out++] = 0x01;
        response[out++] = 0x00;  // TTL 60 seconds
        response[out++] = 0x00;
        response[out++] = 0x00;
        response[out++] = 0x3C;
        response[out++] = 0x00;  // RDLENGTH 4
        response[out++] = 0x04;
        memcpy(response + out, &ap_addr, sizeof(ap_addr));
        out += sizeof(ap_addr);

        sendto(sock,
               response,
               out,
               0,
               reinterpret_cast<sockaddr*>(&source_addr),
               source_len);
    }
}

void startCaptiveDns() {
    if (dns_task != nullptr) {
        return;
    }
    xTaskCreate(captiveDnsTask, "captive_dns", 4096, nullptr, 4, &dns_task);
    ESP_LOGI(TAG, "Captive DNS started for AP clients");
}

void configureCaptiveDhcpOptions() {
    if (ap_netif == nullptr) {
        return;
    }
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = inet_addr(kApIp);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns));

    uint8_t offer_dns = 1;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(ap_netif,
                                                         ESP_NETIF_OP_SET,
                                                         ESP_NETIF_DOMAIN_NAME_SERVER,
                                                         &offer_dns,
                                                         sizeof(offer_dns)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(ap_netif,
                                                         ESP_NETIF_OP_SET,
                                                         ESP_NETIF_CAPTIVEPORTAL_URI,
                                                         const_cast<char*>(kCaptivePortalUrl),
                                                         std::strlen(kCaptivePortalUrl)));
}

void configureSoftAp() {
    wifi_config_t ap_config = {};
    const size_t ssid_len = std::min(std::strlen(provisioning_service_name), sizeof(ap_config.ap.ssid));
    memcpy(ap_config.ap.ssid, provisioning_service_name, ssid_len);
    ap_config.ap.ssid_len = ssid_len;
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
}

void eventHandler(void*, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_PROV_EVENT) {
        if (event_id == WIFI_PROV_CRED_RECV) {
            auto* cfg = static_cast<wifi_sta_config_t*>(event_data);
            copyString(wifi_ssid, sizeof(wifi_ssid), reinterpret_cast<const char*>(cfg->ssid));
            copyString(wifi_password, sizeof(wifi_password), reinterpret_cast<const char*>(cfg->password));
            setInstallerLedMode(InstallerLedMode::Pairing);
            ESP_LOGI(TAG, "Provisioning received SSID=%s", wifi_ssid);
        } else if (event_id == WIFI_PROV_CRED_SUCCESS) {
            setInstallerLedMode(InstallerLedMode::Provisioned);
            ESP_LOGI(TAG, "Provisioning successful");
        } else if (event_id == WIFI_PROV_CRED_FAIL) {
            setInstallerLedMode(InstallerLedMode::Pairing);
            ESP_LOGW(TAG, "Provisioning failed");
        } else if (event_id == WIFI_PROV_END) {
            ESP_LOGI(TAG, "Provisioning end event ignored; BLE provisioning remains available");
        }
        return;
    }
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_AP_START) {
            ESP_LOGI(TAG, "Installer SoftAP started: ssid=%s ip=%s", provisioning_service_name, kApIp);
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            auto* event = static_cast<wifi_event_ap_staconnected_t*>(event_data);
            ESP_LOGI(TAG, "Installer SoftAP client joined: " MACSTR, MAC2STR(event->mac));
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
        setInstallerLedMode(InstallerLedMode::Provisioned);
        ESP_LOGI(TAG, "WiFi connected: ssid=%s ip=%s", wifi_ssid, wifi_ip);
        startWebServer();
    }
}

void startWifiProvisioning() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, eventHandler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventHandler, nullptr));

    esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    buildProvisioningServiceName(provisioning_service_name, sizeof(provisioning_service_name));
    configureCaptiveDhcpOptions();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    configureSoftAp();

    wifi_prov_mgr_config_t prov_config = {};
    wifi_prov_scheme_t ble_apsta_scheme = wifi_prov_scheme_ble;
    ble_apsta_scheme.wifi_mode = WIFI_MODE_APSTA;
    prov_config.scheme = ble_apsta_scheme;
    prov_config.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
    prov_config.app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));
    ESP_ERROR_CHECK(wifi_prov_mgr_disable_auto_stop(1000));

    // Installer firmware should always be discoverable from Espressif's BLE provisioning app,
    // even if this board has stale Wi-Fi provisioning data from an older test.
    wifi_prov_mgr_reset_provisioning();

    const wifi_prov_security1_params_t* security_params = kProvisioningPop;
    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, security_params, provisioning_service_name, nullptr));
    configureSoftAp();
    startCaptiveDns();
    startWebServer();
    ESP_LOGI(TAG,
             "BLE provisioning started. Use Espressif app, service name: %s security=1 pop=%s",
             provisioning_service_name,
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

    startInstallerStatusLed();
    startWifiProvisioning();
    ESP_LOGI(TAG, "Installer WebUI is available on SoftAP %s at http://%s/", provisioning_service_name, kApIp);
}
