#include "web_routes.h"

#include "app_config.h"
#include "build_info.h"
#include "cn105_core.h"
#include "cn105_transport.h"
#include "cn105_uart.h"
#include "device_settings.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "homekit_bridge.h"
#include "platform_fs.h"
#include "platform_log.h"
#include "platform_maintenance.h"
#include "platform_provisioning.h"
#include "platform_wifi.h"
#include "web_http.h"
#include "web_pages.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/time.h>

namespace {

const esp_partition_t* pending_ota_partition = nullptr;

uint64_t uptimeMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

int monthNumber(const char* month) {
    if (month == nullptr) {
        return 0;
    }
    if (std::strcmp(month, "Jan") == 0) return 1;
    if (std::strcmp(month, "Feb") == 0) return 2;
    if (std::strcmp(month, "Mar") == 0) return 3;
    if (std::strcmp(month, "Apr") == 0) return 4;
    if (std::strcmp(month, "May") == 0) return 5;
    if (std::strcmp(month, "Jun") == 0) return 6;
    if (std::strcmp(month, "Jul") == 0) return 7;
    if (std::strcmp(month, "Aug") == 0) return 8;
    if (std::strcmp(month, "Sep") == 0) return 9;
    if (std::strcmp(month, "Oct") == 0) return 10;
    if (std::strcmp(month, "Nov") == 0) return 11;
    if (std::strcmp(month, "Dec") == 0) return 12;
    return 0;
}

uint64_t compileStamp(const char* date, const char* time) {
    char month[4] = {};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(date == nullptr ? "" : date, "%3s %d %d", month, &day, &year) != 3 ||
        std::sscanf(time == nullptr ? "" : time, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return 0;
    }
    const int month_num = monthNumber(month);
    if (month_num <= 0) {
        return 0;
    }
    return static_cast<uint64_t>(year) * 10000000000ULL +
           static_cast<uint64_t>(month_num) * 100000000ULL +
           static_cast<uint64_t>(day) * 1000000ULL +
           static_cast<uint64_t>(hour) * 10000ULL +
           static_cast<uint64_t>(minute) * 100ULL +
           static_cast<uint64_t>(second);
}

uint64_t versionStamp(const char* version) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(version == nullptr ? "" : version,
                    "%4d.%2d%2d.%2d%2d%2d",
                    &year,
                    &month,
                    &day,
                    &hour,
                    &minute,
                    &second) != 6) {
        return 0;
    }
    if (year < 2024 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return 0;
    }
    return static_cast<uint64_t>(year) * 10000000000ULL +
           static_cast<uint64_t>(month) * 100000000ULL +
           static_cast<uint64_t>(day) * 1000000ULL +
           static_cast<uint64_t>(hour) * 10000ULL +
           static_cast<uint64_t>(minute) * 100ULL +
           static_cast<uint64_t>(second);
}

uint64_t appDescStamp(const esp_app_desc_t& desc) {
    const uint64_t stamp_from_version = versionStamp(desc.version);
    return stamp_from_version > 0 ? stamp_from_version : compileStamp(desc.date, desc.time);
}

void versionFromAppDesc(const esp_app_desc_t& desc, char* out, size_t out_len) {
    if (desc.version[0] != '\0') {
        std::snprintf(out, out_len, "%s", desc.version);
        return;
    }

    char month[4] = {};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(desc.date, "%3s %d %d", month, &day, &year) == 3 &&
        std::sscanf(desc.time, "%d:%d:%d", &hour, &minute, &second) == 3) {
        const int month_num = monthNumber(month);
        if (month_num > 0) {
            std::snprintf(out,
                          out_len,
                          "%04d.%02d%02d.%02d%02d%02d",
                          year,
                          month_num,
                          day,
                          hour,
                          minute,
                          second);
            return;
        }
    }
    std::snprintf(out, out_len, "%s", desc.version);
}

uint64_t currentUnixMs(bool* valid) {
    timeval tv {};
    gettimeofday(&tv, nullptr);
    const uint64_t unix_ms = (static_cast<uint64_t>(tv.tv_sec) * 1000ULL) +
                             static_cast<uint64_t>(tv.tv_usec / 1000);
    const bool looks_valid = tv.tv_sec >= 1704067200;  // 2024-01-01 UTC
    if (valid != nullptr) {
        *valid = looks_valid;
    }
    return unix_ms;
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

bool normalizeHomeKitCodeParam(const char* value, char* out_digits, size_t out_len) {
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

void writeMockStateJson(const cn105_core::MockState& state, char* out, size_t out_len) {
    char esc_error[128] = {};
    web_http::jsonEscape(state.lastError, esc_error, sizeof(esc_error));
    std::snprintf(out,
                  out_len,
                  "\"mock_state\":{"
                  "\"connected\":%s,"
                  "\"power\":\"%s\","
                  "\"mode\":\"%s\","
                  "\"target_temperature_f\":%d,"
                  "\"room_temperature_f\":%d,"
                  "\"fan\":\"%s\","
                  "\"vane\":\"%s\","
                  "\"wide_vane\":\"%s\","
                  "\"operating\":%s,"
                  "\"compressor_frequency_hz\":%d,"
                  "\"input_power_w\":%d,"
                  "\"energy_kwh\":%.1f,"
                  "\"last_packet_hex\":\"%s\","
                  "\"last_error\":\"%s\""
                  "}",
                  state.connected ? "true" : "false",
                  state.power,
                  state.mode,
                  state.targetTemperatureF,
                  state.roomTemperatureF,
                  state.fan,
                  state.vane,
                  state.wideVane,
                  state.operating ? "true" : "false",
                  state.compressorFrequencyHz,
                  state.inputPowerW,
                  static_cast<double>(state.energyKwh),
                  state.lastPacketHex,
                  esc_error);
}

void writeHomeKitJson(const homekit_bridge::Status& status, char* out, size_t out_len) {
    char esc_error[128] = {};
    char esc_name[128] = {};
    char esc_model[128] = {};
    char esc_fw[64] = {};
    web_http::jsonEscape(status.lastError, esc_error, sizeof(esc_error));
    web_http::jsonEscape(status.accessoryName, esc_name, sizeof(esc_name));
    web_http::jsonEscape(status.model, esc_model, sizeof(esc_model));
    web_http::jsonEscape(status.firmwareRevision, esc_fw, sizeof(esc_fw));
    std::snprintf(out,
                  out_len,
                  "\"homekit\":{"
                  "\"enabled\":%s,"
                  "\"started\":%s,"
                  "\"paired_controllers\":%d,"
                  "\"accessory_name\":\"%s\","
                  "\"model\":\"%s\","
                  "\"firmware_revision\":\"%s\","
                  "\"setup_code\":\"%s\","
                  "\"setup_id\":\"%s\","
                  "\"setup_payload\":\"%s\","
                  "\"last_event\":\"%s\","
                  "\"last_error\":\"%s\""
                  "}",
                  status.enabled ? "true" : "false",
                  status.started ? "true" : "false",
                  status.pairedControllers,
                  esc_name,
                  esc_model,
                  esc_fw,
                  status.setupCode,
                  status.setupId,
                  status.setupPayload,
                  status.lastEvent,
                  esc_error);
}

esp_err_t rootHandler(httpd_req_t* req) {
    return web_pages::sendRoot(req);
}

esp_err_t debugHandler(httpd_req_t* req) {
    return web_pages::sendDebug(req);
}

esp_err_t logsHandler(httpd_req_t* req) {
    return web_pages::sendLogs(req);
}

esp_err_t filesHandler(httpd_req_t* req) {
    return web_pages::sendFiles(req);
}

const char* downloadFileName(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return "download.bin";
    }
    const char* slash = std::strrchr(path, '/');
    return slash != nullptr && slash[1] != '\0' ? slash + 1 : path;
}

esp_err_t streamFile(httpd_req_t* req, FILE* file, const char* content_type, const char* logical_path) {
    if (file == nullptr) {
        httpd_resp_set_status(req, "404 Not Found");
        return web_http::sendText(req, "text/plain; charset=utf-8", "File not found");
    }

    char disposition[192] = {};
    std::snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", downloadFileName(logical_path));
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    char buffer[app_config::kPersistentLogReadChunkBytes] = {};
    while (true) {
        const size_t read = std::fread(buffer, 1, sizeof(buffer), file);
        if (read > 0) {
            const esp_err_t err = httpd_resp_send_chunk(req, buffer, read);
            if (err != ESP_OK) {
                std::fclose(file);
                return err;
            }
        }
        if (read < sizeof(buffer)) {
            break;
        }
    }
    std::fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t healthHandler(httpd_req_t* req) {
    char body[512] = {};
    char esc_device[128] = {};
    web_http::jsonEscape(device_settings::deviceName(), esc_device, sizeof(esc_device));
    std::snprintf(body,
                  sizeof(body),
                  "{\"ok\":true,\"device\":\"%s\",\"phase\":\"%s\",\"uptime_ms\":%llu}",
                  esc_device,
                  app_config::kPhaseName,
                  static_cast<unsigned long long>(uptimeMs()));

    return web_http::sendText(req, "application/json", body);
}

esp_err_t statusHandler(httpd_req_t* req) {
    const platform_wifi::Status wifi = platform_wifi::getStatus();
    const platform_fs::Status fs = platform_fs::getStatus();
    const cn105_uart::Status cn105 = cn105_uart::getStatus();
    const cn105_core::MockState mock = cn105_core::getMockState();
    const homekit_bridge::Status homekit = homekit_bridge::getStatus();
    const cn105_transport::Status transport = cn105_transport::getStatus();
    const platform_log::Status log = platform_log::getStatus();
    const platform_provisioning::Status provisioning = platform_provisioning::getStatus();
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    const esp_partition_t* boot_partition = esp_ota_get_boot_partition();
    bool server_time_valid = false;
    const uint64_t server_time_unix_ms = currentUnixMs(&server_time_valid);
    const uint64_t boot_time_unix_ms = server_time_valid && server_time_unix_ms >= uptimeMs()
        ? (server_time_unix_ms - uptimeMs())
        : 0;

    char mock_json[768] = {};
    writeMockStateJson(mock, mock_json, sizeof(mock_json));

    char homekit_json[1024] = {};
    writeHomeKitJson(homekit, homekit_json, sizeof(homekit_json));

    const device_settings::Settings& config = device_settings::get();

    char esc_transport_err[128] = {};
    web_http::jsonEscape(transport.lastError, esc_transport_err, sizeof(esc_transport_err));
    char esc_log_path[160] = {};
    web_http::jsonEscape(log.currentPath, esc_log_path, sizeof(esc_log_path));
    char esc_device_name[128] = {};
    char esc_version[64] = {};
    char esc_config_name[128] = {};
    char esc_config_wifi_ssid[96] = {};
    char esc_config_hk_mfr[128] = {};
    char esc_config_hk_model[128] = {};
    char esc_config_hk_serial[128] = {};
    char esc_config_hk_setup_id[16] = {};
    char esc_homekit_code[32] = {};
    char esc_prov_stage[32] = {};
    char esc_prov_result[32] = {};
    char esc_prov_service[64] = {};
    char esc_prov_ssid[96] = {};
    web_http::jsonEscape(device_settings::deviceName(), esc_device_name, sizeof(esc_device_name));
    web_http::jsonEscape(build_info::firmwareVersion(), esc_version, sizeof(esc_version));
    web_http::jsonEscape(config.deviceName, esc_config_name, sizeof(esc_config_name));
    web_http::jsonEscape(config.wifiSsid, esc_config_wifi_ssid, sizeof(esc_config_wifi_ssid));
    web_http::jsonEscape(config.homeKitManufacturer, esc_config_hk_mfr, sizeof(esc_config_hk_mfr));
    web_http::jsonEscape(config.homeKitModel, esc_config_hk_model, sizeof(esc_config_hk_model));
    web_http::jsonEscape(config.homeKitSerial, esc_config_hk_serial, sizeof(esc_config_hk_serial));
    web_http::jsonEscape(config.homeKitSetupId, esc_config_hk_setup_id, sizeof(esc_config_hk_setup_id));
    web_http::jsonEscape(device_settings::homeKitDisplayCode(), esc_homekit_code, sizeof(esc_homekit_code));
    web_http::jsonEscape(provisioning.stage, esc_prov_stage, sizeof(esc_prov_stage));
    web_http::jsonEscape(provisioning.lastResult, esc_prov_result, sizeof(esc_prov_result));
    web_http::jsonEscape(provisioning.serviceName, esc_prov_service, sizeof(esc_prov_service));
    web_http::jsonEscape(provisioning.pendingSsid, esc_prov_ssid, sizeof(esc_prov_ssid));

    constexpr size_t kStatusBodyLen = 10240;
    char* body = static_cast<char*>(std::calloc(kStatusBodyLen, sizeof(char)));
    if (body == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response allocation failed");
    }

    const char* transport_mode = device_settings::useRealCn105() ? "real" : "mock";

    std::snprintf(body,
                  kStatusBodyLen,
                  "{"
                  "\"ok\":true,"
                  "\"device\":\"%s\","
                  "\"version\":\"%s\","
                  "\"phase\":\"%s\","
                  "\"server_time_unix_ms\":%llu,"
                  "\"boot_time_unix_ms\":%llu,"
                  "\"boot_time_valid\":%s,"
                  "\"uptime_ms\":%llu,"
                  "\"config\":{"
                  "\"device_name\":\"%s\","
                  "\"wifi_ssid\":\"%s\","
                  "\"wifi_password_set\":%s,"
                  "\"homekit_code\":\"%s\","
                  "\"homekit_manufacturer\":\"%s\","
                  "\"homekit_model\":\"%s\","
                  "\"homekit_serial\":\"%s\","
                  "\"homekit_setup_id\":\"%s\","
                  "\"led_enabled\":%s,"
                  "\"led_pin\":%d,"
                  "\"cn105_mode\":\"%s\","
                  "\"cn105_rx_pin\":%d,"
                  "\"cn105_tx_pin\":%d,"
                  "\"cn105_baud\":%d,"
                  "\"cn105_data_bits\":%d,"
                  "\"cn105_parity\":\"%c\","
                  "\"cn105_stop_bits\":%d,"
                  "\"cn105_rx_pullup\":%s,"
                  "\"cn105_tx_open_drain\":%s,"
                  "\"poll_active_ms\":%lu,"
                  "\"poll_off_ms\":%lu,"
                  "\"log_level\":\"%s\"},"
                  "\"wifi\":{\"initialized\":%s,\"connected\":%s,\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"channel\":%d,\"mac\":\"%s\",\"bssid\":\"%s\",\"last_event\":\"%s\",\"last_event_age_ms\":%lu},"
                  "\"provisioning\":{\"initialized\":%s,\"active\":%s,\"credentials_received\":%s,\"reboot_pending\":%s,\"remaining_ms\":%lu,\"button_gpio\":%d,\"stage\":\"%s\",\"last_result\":\"%s\",\"service_name\":\"%s\",\"pending_ssid\":\"%s\"},"
                  "%s,"
                  "\"filesystem\":{\"mounted\":%s,\"base_path\":\"%s\",\"total_bytes\":%u,\"used_bytes\":%u,\"free_bytes\":%u},"
                  "\"ota\":{\"running_partition\":\"%s\",\"boot_partition\":\"%s\",\"running_address\":%u,\"boot_address\":%u},"
                  "\"log\":{\"active\":%s,\"current\":\"%s\",\"current_bytes\":%u,\"dropped_lines\":%u,\"level\":\"%s\"},"
                  "\"cn105\":{\"uart_initialized\":%s,\"uart\":%d,\"rx_pin\":%d,\"tx_pin\":%d,\"baud\":%d,\"format\":\"%s\",\"transport\":\"%s\","
                  "\"transport_status\":{\"running\":%s,\"connected\":%s,\"phase\":\"%s\",\"connect_attempts\":%lu,\"poll_cycles\":%lu,\"rx_packets\":%lu,\"rx_errors\":%lu,\"tx_packets\":%lu,\"sets_pending\":%lu,\"last_error\":\"%s\"},"
                  "%s}"
                  "}",
                  esc_device_name,
                  esc_version,
                  app_config::kPhaseName,
                  static_cast<unsigned long long>(server_time_unix_ms),
                  static_cast<unsigned long long>(boot_time_unix_ms),
                  server_time_valid ? "true" : "false",
                  static_cast<unsigned long long>(uptimeMs()),
                  esc_config_name,
                  esc_config_wifi_ssid,
                  config.wifiPassword[0] == '\0' ? "false" : "true",
                  esc_homekit_code,
                  esc_config_hk_mfr,
                  esc_config_hk_model,
                  esc_config_hk_serial,
                  esc_config_hk_setup_id,
                  config.statusLedEnabled ? "true" : "false",
                  config.statusLedPin,
                  transport_mode,
                  config.cn105RxPin,
                  config.cn105TxPin,
                  config.cn105BaudRate,
                  config.cn105DataBits,
                  config.cn105Parity,
                  config.cn105StopBits,
                  config.cn105RxPullupEnabled ? "true" : "false",
                  config.cn105TxOpenDrain ? "true" : "false",
                  static_cast<unsigned long>(config.pollIntervalActiveMs),
                  static_cast<unsigned long>(config.pollIntervalOffMs),
                  device_settings::logLevelName(),
                  wifi.initialized ? "true" : "false",
                  wifi.staConnected ? "true" : "false",
                  wifi.mode,
                  wifi.ssid,
                  wifi.ip,
                  wifi.rssi,
                  wifi.channel,
                  wifi.mac,
                  wifi.bssid,
                  wifi.lastEvent,
                  static_cast<unsigned long>(wifi.lastEventAgeMs),
                  provisioning.initialized ? "true" : "false",
                  provisioning.active ? "true" : "false",
                  provisioning.credentialsReceived ? "true" : "false",
                  provisioning.rebootPending ? "true" : "false",
                  static_cast<unsigned long>(provisioning.remainingMs),
                  provisioning.buttonGpio,
                  esc_prov_stage,
                  esc_prov_result,
                  esc_prov_service,
                  esc_prov_ssid,
                  homekit_json,
                  fs.mounted ? "true" : "false",
                  platform_fs::basePath(),
                  static_cast<unsigned>(fs.totalBytes),
                  static_cast<unsigned>(fs.usedBytes),
                  static_cast<unsigned>(fs.freeBytes),
                  running_partition == nullptr ? "" : running_partition->label,
                  boot_partition == nullptr ? "" : boot_partition->label,
                  static_cast<unsigned>(running_partition == nullptr ? 0 : running_partition->address),
                  static_cast<unsigned>(boot_partition == nullptr ? 0 : boot_partition->address),
                  log.active ? "true" : "false",
                  esc_log_path,
                  static_cast<unsigned>(log.currentBytes),
                  static_cast<unsigned>(log.droppedLines),
                  log.levelName,
                  cn105.initialized ? "true" : "false",
                  cn105.uart,
                  cn105.rxPin,
                  cn105.txPin,
                  cn105.baudRate,
                  cn105.format,
                  transport_mode,
                  transport.taskRunning ? "true" : "false",
                  transport.connected ? "true" : "false",
                  transport.phase,
                  static_cast<unsigned long>(transport.connectAttempts),
                  static_cast<unsigned long>(transport.pollCycles),
                  static_cast<unsigned long>(transport.rxPackets),
                  static_cast<unsigned long>(transport.rxErrors),
                  static_cast<unsigned long>(transport.txPackets),
                  static_cast<unsigned long>(transport.setsPending),
                  esc_transport_err,
                  mock_json);

    const esp_err_t err = web_http::sendText(req, "application/json", body);
    std::free(body);
    return err;
}

esp_err_t cn105MockStatusHandler(httpd_req_t* req) {
    const cn105_core::MockState mock = cn105_core::getMockState();
    char mock_json[768] = {};
    writeMockStateJson(mock, mock_json, sizeof(mock_json));

    char body[1024] = {};
    std::snprintf(body, sizeof(body), "{\"ok\":true,\"transport\":\"mock\",%s}", mock_json);
    return web_http::sendText(req, "application/json", body);
}

esp_err_t cn105BuildSetHandler(httpd_req_t* req) {
    const cn105_core::MockState mock = cn105_core::getMockState();
    char query[512] = {};
    web_http::readQuery(req, query, sizeof(query));

    cn105_core::SetCommand command{};
    char power[16] = {};
    bool any = false;

    if (web_http::queryValue(query, "power", power, sizeof(power))) {
        command.hasPower = true;
        command.power = power;
        any = true;
    }

    char mode[16] = {};
    if (web_http::queryValue(query, "mode", mode, sizeof(mode))) {
        command.hasMode = true;
        command.mode = mode;
        any = true;
    }

    char temp[16] = {};
    if (web_http::queryValue(query, "temperature_f", temp, sizeof(temp)) || web_http::queryValue(query, "temp_f", temp, sizeof(temp))) {
        command.hasTemperatureF = true;
        command.temperatureF = std::atoi(temp);
        any = true;
    }

    char fan[16] = {};
    if (web_http::queryValue(query, "fan", fan, sizeof(fan))) {
        command.hasFan = true;
        command.fan = fan;
        any = true;
    }

    char vane[16] = {};
    if (web_http::queryValue(query, "vane", vane, sizeof(vane))) {
        command.hasVane = true;
        command.vane = vane;
        any = true;
    }

    char wide_vane[32] = {};
    if (web_http::queryValue(query, "wide_vane", wide_vane, sizeof(wide_vane))) {
        command.hasWideVane = true;
        command.wideVane = wide_vane;
        any = true;
    }

    if (!any) {
        command.hasPower = true;
        command.power = mock.power;
        command.hasMode = true;
        command.mode = mock.mode;
        command.hasTemperatureF = true;
        command.temperatureF = mock.targetTemperatureF;
        command.hasFan = true;
        command.fan = mock.fan;
        command.hasVane = true;
        command.vane = mock.vane;
        command.hasWideVane = true;
        command.wideVane = mock.wideVane;
    }

    cn105_core::Packet packet{};
    char error[96] = {};
    if (!cn105_core::buildSetPacket(command, &packet, error, sizeof(error))) {
        return web_http::sendJsonError(req, error);
    }

    cn105_core::DecodedPacket decoded{};
    if (!cn105_core::decodePacket(packet.bytes, packet.length, &decoded, error, sizeof(error))) {
        return web_http::sendJsonError(req, error);
    }

    char apply_value[8] = {};
    const bool apply = req->method == HTTP_POST ||
        (web_http::queryValue(query, "apply", apply_value, sizeof(apply_value)) && std::strcmp(apply_value, "1") == 0);
    if (apply) {
        if (device_settings::useRealCn105()) {
            if (!cn105_transport::queueSetCommand(command)) {
                return web_http::sendJsonError(req, "transport queue full");
            }
        } else if (!cn105_core::applySetPacketToMock(packet.bytes, packet.length, error, sizeof(error))) {
            return web_http::sendJsonError(req, error);
        }
    }

    char packet_hex[cn105_core::kMaxHexLen] = {};
    cn105_core::bytesToHex(packet.bytes, packet.length, packet_hex, sizeof(packet_hex));

    const cn105_core::MockState state = cn105_core::getMockState();
    char mock_json[768] = {};
    writeMockStateJson(state, mock_json, sizeof(mock_json));

    char body[1536] = {};
    std::snprintf(body,
                  sizeof(body),
                  "{\"ok\":true,\"applied\":%s,\"packet_hex\":\"%s\",\"decoded\":{\"type\":\"%s\",\"summary\":\"%s\"},%s}",
                  apply ? "true" : "false",
                  packet_hex,
                  decoded.type,
                  decoded.summary,
                  mock_json);
    return web_http::sendText(req, "application/json", body);
}

esp_err_t cn105DecodeHandler(httpd_req_t* req) {
    char query[768] = {};
    if (!web_http::readQuery(req, query, sizeof(query))) {
        return web_http::sendJsonError(req, "missing query string");
    }

    char hex[256] = {};
    if (!web_http::queryValue(query, "hex", hex, sizeof(hex))) {
        return web_http::sendJsonError(req, "missing hex parameter");
    }

    uint8_t bytes[cn105_core::kPacketLen] = {};
    size_t len = 0;
    char error[96] = {};
    if (!cn105_core::parseHex(hex, bytes, sizeof(bytes), &len, error, sizeof(error))) {
        return web_http::sendJsonError(req, error);
    }

    cn105_core::DecodedPacket decoded{};
    if (!cn105_core::decodePacket(bytes, len, &decoded, error, sizeof(error))) {
        return web_http::sendJsonError(req, error);
    }

    char normalized_hex[cn105_core::kMaxHexLen] = {};
    cn105_core::bytesToHex(bytes, len, normalized_hex, sizeof(normalized_hex));

    char body[768] = {};
    std::snprintf(body,
                  sizeof(body),
                  "{\"ok\":true,\"packet_hex\":\"%s\",\"decoded\":{\"checksum_ok\":%s,\"command\":%u,\"type\":\"%s\",\"info_code\":%u,\"summary\":\"%s\"}}",
                  normalized_hex,
                  decoded.checksumOk ? "true" : "false",
                  decoded.command,
                  decoded.type,
                  decoded.infoCode,
                  decoded.summary);
    return web_http::sendText(req, "application/json", body);
}

esp_err_t adminHandler(httpd_req_t* req) {
    return web_pages::sendAdmin(req);
}

esp_err_t configSaveHandler(httpd_req_t* req) {
    char body[1536] = {};
    if (!readBody(req, body, sizeof(body))) {
        return web_http::sendJsonError(req, "failed to read request body");
    }

    device_settings::Settings next = device_settings::get();

    char value[192] = {};
    if (web_http::queryValue(body, "device_name", value, sizeof(value))) {
        std::strncpy(next.deviceName, value, sizeof(next.deviceName) - 1);
        next.deviceName[sizeof(next.deviceName) - 1] = '\0';
    }
    if (web_http::queryValue(body, "wifi_ssid", value, sizeof(value))) {
        std::strncpy(next.wifiSsid, value, sizeof(next.wifiSsid) - 1);
        next.wifiSsid[sizeof(next.wifiSsid) - 1] = '\0';
    }
    if (web_http::queryValue(body, "wifi_password", value, sizeof(value))) {
        std::strncpy(next.wifiPassword, value, sizeof(next.wifiPassword) - 1);
        next.wifiPassword[sizeof(next.wifiPassword) - 1] = '\0';
    }
    if (web_http::queryValue(body, "homekit_code", value, sizeof(value))) {
        char digits[sizeof(next.homeKitCode)] = {};
        if (!normalizeHomeKitCodeParam(value, digits, sizeof(digits))) {
            return web_http::sendJsonError(req, "invalid HomeKit setup code");
        }
        std::strncpy(next.homeKitCode, digits, sizeof(next.homeKitCode) - 1);
        next.homeKitCode[sizeof(next.homeKitCode) - 1] = '\0';
    }
    if (web_http::queryValue(body, "homekit_manufacturer", value, sizeof(value))) {
        std::strncpy(next.homeKitManufacturer, value, sizeof(next.homeKitManufacturer) - 1);
        next.homeKitManufacturer[sizeof(next.homeKitManufacturer) - 1] = '\0';
    }
    if (web_http::queryValue(body, "homekit_model", value, sizeof(value))) {
        std::strncpy(next.homeKitModel, value, sizeof(next.homeKitModel) - 1);
        next.homeKitModel[sizeof(next.homeKitModel) - 1] = '\0';
    }
    if (web_http::queryValue(body, "homekit_serial", value, sizeof(value))) {
        std::strncpy(next.homeKitSerial, value, sizeof(next.homeKitSerial) - 1);
        next.homeKitSerial[sizeof(next.homeKitSerial) - 1] = '\0';
    }
    if (web_http::queryValue(body, "homekit_setup_id", value, sizeof(value))) {
        std::strncpy(next.homeKitSetupId, value, sizeof(next.homeKitSetupId) - 1);
        next.homeKitSetupId[sizeof(next.homeKitSetupId) - 1] = '\0';
    }
    if (web_http::queryValue(body, "led_enabled", value, sizeof(value))) {
        next.statusLedEnabled = std::strcmp(value, "0") != 0 &&
                                std::strcmp(value, "false") != 0 &&
                                std::strcmp(value, "off") != 0;
    }
    if (web_http::queryValue(body, "led_pin", value, sizeof(value))) {
        next.statusLedPin = std::atoi(value);
    }
    if (web_http::queryValue(body, "cn105_mode", value, sizeof(value))) {
        next.useRealCn105 = std::strcmp(value, "mock") != 0;
    }
    if (web_http::queryValue(body, "cn105_rx_pin", value, sizeof(value))) {
        next.cn105RxPin = std::atoi(value);
    }
    if (web_http::queryValue(body, "cn105_tx_pin", value, sizeof(value))) {
        next.cn105TxPin = std::atoi(value);
    }
    if (web_http::queryValue(body, "cn105_baud", value, sizeof(value))) {
        next.cn105BaudRate = std::atoi(value);
    }
    if (web_http::queryValue(body, "cn105_data_bits", value, sizeof(value))) {
        next.cn105DataBits = std::atoi(value);
    }
    if (web_http::queryValue(body, "cn105_parity", value, sizeof(value))) {
        char parity = 'E';
        if (!device_settings::parseCn105Parity(value, &parity)) {
            return web_http::sendJsonError(req, "invalid CN105 parity");
        }
        next.cn105Parity = parity;
    }
    if (web_http::queryValue(body, "cn105_stop_bits", value, sizeof(value))) {
        next.cn105StopBits = std::atoi(value);
    }
    if (web_http::queryValue(body, "cn105_rx_pullup", value, sizeof(value))) {
        next.cn105RxPullupEnabled = std::strcmp(value, "0") != 0 &&
                                    std::strcmp(value, "false") != 0 &&
                                    std::strcmp(value, "off") != 0;
    }
    if (web_http::queryValue(body, "cn105_tx_open_drain", value, sizeof(value))) {
        next.cn105TxOpenDrain = std::strcmp(value, "0") != 0 &&
                                std::strcmp(value, "false") != 0 &&
                                std::strcmp(value, "off") != 0;
    }
    if (web_http::queryValue(body, "poll_active_ms", value, sizeof(value))) {
        next.pollIntervalActiveMs = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    }
    if (web_http::queryValue(body, "poll_off_ms", value, sizeof(value))) {
        next.pollIntervalOffMs = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    }
    if (web_http::queryValue(body, "log_level", value, sizeof(value))) {
        esp_log_level_t level = ESP_LOG_INFO;
        if (!device_settings::parseLogLevel(value, &level)) {
            return web_http::sendJsonError(req, "invalid log level");
        }
        next.logLevel = level;
    }

    bool reboot_required = false;
    char message[192] = {};
    if (!device_settings::save(next, &reboot_required, message, sizeof(message))) {
        return web_http::sendJsonError(req, message);
    }

    platform_log::applyConfiguredLogLevel();

    char escaped[256] = {};
    web_http::jsonEscape(message, escaped, sizeof(escaped));
    char response[512] = {};
    std::snprintf(response,
                  sizeof(response),
                  "{\"ok\":true,\"reboot_required\":%s,\"message\":\"%s\"}",
                  reboot_required ? "true" : "false",
                  escaped);
    return web_http::sendText(req, "application/json", response);
}

esp_err_t assetHandler(httpd_req_t* req) {
    return web_pages::sendAsset(req);
}

esp_err_t logsListHandler(httpd_req_t* req) {
    const platform_log::Status status = platform_log::getStatus();
    std::string body = "{\"ok\":true,\"active\":";
    body += status.active ? "true" : "false";
    body += ",\"current\":\"";
    body += platform_fs::jsonEscape(status.currentPath);
    body += "\",\"current_bytes\":";
    body += std::to_string(status.currentBytes);
    body += ",\"level\":\"";
    body += platform_fs::jsonEscape(status.levelName);
    body += "\"";
    body += ",\"dropped_lines\":";
    body += std::to_string(status.droppedLines);
    body += ",\"logs\":";
    body += platform_log::logsJson();
    body += "}";
    return web_http::sendText(req, "application/json", body.c_str());
}

esp_err_t logFileHandler(httpd_req_t* req) {
    char query[256] = {};
    web_http::readQuery(req, query, sizeof(query));
    char file_name[160] = {};
    web_http::queryValue(query, "file", file_name, sizeof(file_name));

    FILE* file = nullptr;
    if (!platform_log::openLogFile(file_name, &file)) {
        httpd_resp_set_status(req, "404 Not Found");
        return web_http::sendText(req, "text/plain; charset=utf-8", "Log file not found");
    }

    char normalized[160] = {};
    platform_fs::normalizePath(file_name[0] == '\0' ? platform_log::getStatus().currentPath : file_name, normalized, sizeof(normalized));
    return streamFile(req, file, "text/plain; charset=utf-8", normalized);
}

esp_err_t liveLogHandler(httpd_req_t* req) {
    char query[128] = {};
    web_http::readQuery(req, query, sizeof(query));
    char offset_value[32] = {};
    size_t offset = 0;
    if (web_http::queryValue(query, "offset", offset_value, sizeof(offset_value))) {
        offset = static_cast<size_t>(std::strtoul(offset_value, nullptr, 10));
    }

    char text[app_config::kPersistentLogReadChunkBytes + 1] = {};
    size_t next_offset = 0;
    size_t file_size = 0;
    bool reset = false;
    if (!platform_log::readLiveLog(offset,
                                   app_config::kPersistentLogReadChunkBytes,
                                   text,
                                   sizeof(text),
                                   &next_offset,
                                   &file_size,
                                   &reset)) {
        return web_http::sendJsonError(req, "active log file not found");
    }

    const std::string escaped = platform_fs::jsonEscape(text);
    std::string body = "{\"ok\":true,\"size\":";
    body += std::to_string(file_size);
    body += ",\"nextOffset\":";
    body += std::to_string(next_offset);
    body += ",\"reset\":";
    body += reset ? "true" : "false";
    body += ",\"text\":\"";
    body += escaped;
    body += "\"}";
    return web_http::sendText(req, "application/json", body.c_str());
}

esp_err_t filesListHandler(httpd_req_t* req) {
    char query[256] = {};
    web_http::readQuery(req, query, sizeof(query));
    char dir[160] = "/";
    web_http::queryValue(query, "dir", dir, sizeof(dir));
    const std::string body = platform_fs::listJson(dir);
    return web_http::sendText(req, "application/json", body.c_str());
}

esp_err_t fileDownloadHandler(httpd_req_t* req) {
    char query[256] = {};
    web_http::readQuery(req, query, sizeof(query));
    char path[160] = {};
    if (!web_http::queryValue(query, "path", path, sizeof(path))) {
        return web_http::sendJsonError(req, "missing path");
    }

    FILE* file = platform_fs::openRead(path);
    char normalized[160] = {};
    platform_fs::normalizePath(path, normalized, sizeof(normalized));
    return streamFile(req, file, "application/octet-stream", normalized);
}

esp_err_t fileDeleteHandler(httpd_req_t* req) {
    char query[256] = {};
    web_http::readQuery(req, query, sizeof(query));
    char path[160] = {};
    if (!web_http::queryValue(query, "path", path, sizeof(path))) {
        return web_http::sendJsonError(req, "missing path");
    }

    char message[96] = {};
    const bool ok = platform_fs::removePath(path, message, sizeof(message));
    char escaped[128] = {};
    web_http::jsonEscape(message, escaped, sizeof(escaped));
    char body[192] = {};
    std::snprintf(body, sizeof(body), "{\"ok\":%s,\"message\":\"%s\"}", ok ? "true" : "false", escaped);
    if (!ok) {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    return web_http::sendText(req, "application/json", body);
}

esp_err_t fileCreateHandler(httpd_req_t* req) {
    char query[256] = {};
    web_http::readQuery(req, query, sizeof(query));
    char path[160] = {};
    if (!web_http::queryValue(query, "path", path, sizeof(path))) {
        return web_http::sendJsonError(req, "missing path");
    }

    char message[96] = {};
    const bool ok = platform_fs::createFile(path, "", 0, message, sizeof(message));
    char escaped[128] = {};
    web_http::jsonEscape(message, escaped, sizeof(escaped));
    char body[192] = {};
    std::snprintf(body, sizeof(body), "{\"ok\":%s,\"message\":\"%s\"}", ok ? "true" : "false", escaped);
    if (!ok) {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    return web_http::sendText(req, "application/json", body);
}

esp_err_t dirCreateHandler(httpd_req_t* req) {
    char query[256] = {};
    web_http::readQuery(req, query, sizeof(query));
    char path[160] = {};
    if (!web_http::queryValue(query, "path", path, sizeof(path))) {
        return web_http::sendJsonError(req, "missing path");
    }

    char message[96] = {};
    const bool ok = platform_fs::createDirectory(path, message, sizeof(message));
    char escaped[128] = {};
    web_http::jsonEscape(message, escaped, sizeof(escaped));
    char body[192] = {};
    std::snprintf(body, sizeof(body), "{\"ok\":%s,\"message\":\"%s\"}", ok ? "true" : "false", escaped);
    if (!ok) {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    return web_http::sendText(req, "application/json", body);
}

esp_err_t fileUploadHandler(httpd_req_t* req) {
    char query[256] = {};
    web_http::readQuery(req, query, sizeof(query));
    char path[160] = {};
    if (!web_http::queryValue(query, "path", path, sizeof(path))) {
        return web_http::sendJsonError(req, "missing path");
    }

    const platform_fs::Status fs = platform_fs::getStatus();
    const size_t existing = platform_fs::fileSize(path);
    if (req->content_len > fs.freeBytes + existing) {
        return web_http::sendJsonError(req, "not enough SPIFFS space for upload");
    }

    FILE* file = platform_fs::openWrite(path, "w");
    if (file == nullptr) {
        return web_http::sendJsonError(req, "failed to open upload file");
    }

    char buffer[app_config::kPersistentLogReadChunkBytes] = {};
    int remaining = req->content_len;
    size_t written_total = 0;
    while (remaining > 0) {
        const int recv = httpd_req_recv(req, buffer, std::min<int>(remaining, sizeof(buffer)));
        if (recv <= 0) {
            std::fclose(file);
            return web_http::sendJsonError(req, "upload receive failed");
        }
        const size_t written = std::fwrite(buffer, 1, static_cast<size_t>(recv), file);
        written_total += written;
        remaining -= recv;
        if (written < static_cast<size_t>(recv)) {
            std::fclose(file);
            return web_http::sendJsonError(req, "partial upload write");
        }
    }
    std::fclose(file);

    char normalized[160] = {};
    platform_fs::normalizePath(path, normalized, sizeof(normalized));
    std::string body = "{\"ok\":true,\"path\":\"";
    body += platform_fs::jsonEscape(normalized);
    body += "\",\"bytes\":";
    body += std::to_string(written_total);
    body += "}";
    return web_http::sendText(req, "application/json", body.c_str());
}

esp_err_t rebootHandler(httpd_req_t* req) {
    web_http::sendText(req, "application/json", "{\"ok\":true,\"message\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t sendMaintenanceResult(httpd_req_t* req, const platform_maintenance::Result& result) {
    char escaped_message[640] = {};
    web_http::jsonEscape(result.message, escaped_message, sizeof(escaped_message));

    char body[896] = {};
    std::snprintf(body,
                  sizeof(body),
                  "{\"ok\":%s,\"action\":\"%s\",\"rebooting\":%s,\"message\":\"%s\"}",
                  result.ok ? "true" : "false",
                  result.action,
                  result.rebooting ? "true" : "false",
                  escaped_message);
    if (!result.ok) {
        httpd_resp_set_status(req, "500 Internal Server Error");
    }
    return web_http::sendText(req, "application/json", body);
}

esp_err_t resetHomeKitHandler(httpd_req_t* req) {
    return sendMaintenanceResult(req, platform_maintenance::resetHomeKit());
}

esp_err_t clearLogsHandler(httpd_req_t* req) {
    return sendMaintenanceResult(req, platform_maintenance::clearLogs());
}

esp_err_t clearSpiffsHandler(httpd_req_t* req) {
    return sendMaintenanceResult(req, platform_maintenance::clearSpiffs());
}

esp_err_t clearAllNvsHandler(httpd_req_t* req) {
    return sendMaintenanceResult(req, platform_maintenance::clearAllNvs());
}

esp_err_t otaUploadHandler(httpd_req_t* req) {
    if (req->content_len <= 0) {
        return web_http::sendJsonError(req, "empty OTA upload");
    }

    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (update_partition == nullptr) {
        return web_http::sendJsonError(req, "no OTA update partition available");
    }
    if (running_partition != nullptr && update_partition->address == running_partition->address) {
        return web_http::sendJsonError(req, "OTA update partition matches running partition");
    }
    if (static_cast<size_t>(req->content_len) > update_partition->size) {
        return web_http::sendJsonError(req, "uploaded app is larger than OTA partition");
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        char message[128] = {};
        std::snprintf(message, sizeof(message), "esp_ota_begin failed: %s", esp_err_to_name(err));
        return web_http::sendJsonError(req, message);
    }

    uint8_t buffer[app_config::kPersistentLogReadChunkBytes] = {};
    int remaining = req->content_len;
    size_t written_total = 0;
    while (remaining > 0) {
        const int recv = httpd_req_recv(req, reinterpret_cast<char*>(buffer), std::min<int>(remaining, sizeof(buffer)));
        if (recv <= 0) {
            esp_ota_abort(ota_handle);
            return web_http::sendJsonError(req, "OTA upload receive failed");
        }

        err = esp_ota_write(ota_handle, buffer, static_cast<size_t>(recv));
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            char message[128] = {};
            std::snprintf(message, sizeof(message), "esp_ota_write failed: %s", esp_err_to_name(err));
            return web_http::sendJsonError(req, message);
        }

        written_total += static_cast<size_t>(recv);
        remaining -= recv;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        char message[128] = {};
        std::snprintf(message, sizeof(message), "esp_ota_end failed: %s", esp_err_to_name(err));
        return web_http::sendJsonError(req, message);
    }

    esp_app_desc_t uploaded_desc = {};
    err = esp_ota_get_partition_description(update_partition, &uploaded_desc);
    if (err != ESP_OK) {
        char message[128] = {};
        std::snprintf(message, sizeof(message), "app description read failed: %s", esp_err_to_name(err));
        return web_http::sendJsonError(req, message);
    }

    const esp_app_desc_t* current_desc = esp_app_get_description();
    if (current_desc != nullptr && std::strcmp(uploaded_desc.project_name, current_desc->project_name) != 0) {
        char message[192] = {};
        std::snprintf(message,
                      sizeof(message),
                      "project mismatch: uploaded=%s current=%s",
                      uploaded_desc.project_name,
                      current_desc->project_name);
        return web_http::sendJsonError(req, message);
    }

    char uploaded_version[32] = {};
    char current_version[32] = {};
    versionFromAppDesc(uploaded_desc, uploaded_version, sizeof(uploaded_version));
    std::snprintf(current_version, sizeof(current_version), "%s", build_info::firmwareVersion());

    const uint64_t uploaded_stamp = appDescStamp(uploaded_desc);
    uint64_t current_stamp = versionStamp(current_version);
    if (current_stamp == 0 && current_desc != nullptr) {
        current_stamp = appDescStamp(*current_desc);
    }
    const bool rollback = uploaded_stamp > 0 && current_stamp > 0 && uploaded_stamp < current_stamp;
    const bool same_or_older = uploaded_stamp > 0 && current_stamp > 0 && uploaded_stamp <= current_stamp;
    pending_ota_partition = update_partition;

    char body[768] = {};
    std::snprintf(body,
                  sizeof(body),
                  "{\"ok\":true,"
                  "\"message\":\"OTA image written. Reboot to boot the uploaded firmware.\","
                  "\"bytes\":%u,"
                  "\"partition\":\"%s\","
                  "\"current_version\":\"%s\","
                  "\"uploaded_version\":\"%s\","
                  "\"rollback\":%s,"
                  "\"same_or_older\":%s,"
                  "\"warning\":\"%s\"}",
                  static_cast<unsigned>(written_total),
                  update_partition->label,
                  current_version,
                  uploaded_version,
                  rollback ? "true" : "false",
                  same_or_older ? "true" : "false",
                  same_or_older ? "Uploaded firmware is not newer than the running firmware. Reboot is still allowed." : "");
    return web_http::sendText(req, "application/json", body);
}

esp_err_t otaApplyHandler(httpd_req_t* req) {
    if (pending_ota_partition == nullptr) {
        return web_http::sendJsonError(req, "no pending OTA upload");
    }

    const esp_partition_t* partition = pending_ota_partition;
    pending_ota_partition = nullptr;

    const esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        char message[128] = {};
        std::snprintf(message, sizeof(message), "set boot partition failed: %s", esp_err_to_name(err));
        return web_http::sendJsonError(req, message);
    }

    web_http::sendText(req, "application/json", "{\"ok\":true,\"message\":\"rebooting into uploaded OTA firmware\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

const web_http::Route ROUTES[] = {
    { "/", HTTP_GET, rootHandler },
    { "/debug", HTTP_GET, debugHandler },
    { "/logs", HTTP_GET, logsHandler },
    { "/files", HTTP_GET, filesHandler },
    { "/admin", HTTP_GET, adminHandler },
    { "/assets/*", HTTP_GET, assetHandler },
    { "/api/health", HTTP_GET, healthHandler },
    { "/api/status", HTTP_GET, statusHandler },
    { "/api/reboot", HTTP_POST, rebootHandler },
    { "/api/config/save", HTTP_POST, configSaveHandler },
    { "/api/maintenance/reset-homekit", HTTP_POST, resetHomeKitHandler },
    { "/api/maintenance/clear-logs", HTTP_POST, clearLogsHandler },
    { "/api/maintenance/clear-spiffs", HTTP_POST, clearSpiffsHandler },
    { "/api/maintenance/clear-all-nvs", HTTP_POST, clearAllNvsHandler },
    { "/api/ota/upload", HTTP_POST, otaUploadHandler },
    { "/api/ota/apply", HTTP_POST, otaApplyHandler },
    { "/api/logs", HTTP_GET, logsListHandler },
    { "/api/log/file", HTTP_GET, logFileHandler },
    { "/api/log/live", HTTP_GET, liveLogHandler },
    { "/api/files", HTTP_GET, filesListHandler },
    { "/api/files/download", HTTP_GET, fileDownloadHandler },
    { "/api/files/delete", HTTP_POST, fileDeleteHandler },
    { "/api/files/create-file", HTTP_POST, fileCreateHandler },
    { "/api/files/create-dir", HTTP_POST, dirCreateHandler },
    { "/api/files/upload", HTTP_POST, fileUploadHandler },
    { "/api/cn105/mock/status", HTTP_GET, cn105MockStatusHandler },
    { "/api/cn105/mock/build-set", HTTP_GET, cn105BuildSetHandler },
    { "/api/cn105/mock/build-set", HTTP_POST, cn105BuildSetHandler },
    { "/api/cn105/decode", HTTP_GET, cn105DecodeHandler },
};

}  // namespace

namespace web_routes {

esp_err_t registerRoutes(httpd_handle_t server) {
    return web_http::registerRoutes(server, ROUTES, sizeof(ROUTES) / sizeof(ROUTES[0]));
}

}  // namespace web_routes
