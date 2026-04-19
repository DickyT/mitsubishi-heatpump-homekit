#include "web_routes.h"

#include "app_config.h"
#include "cn105_core.h"
#include "cn105_transport.h"
#include "cn105_uart.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "homekit_bridge.h"
#include "platform_fs.h"
#include "platform_log.h"
#include "platform_maintenance.h"
#include "platform_wifi.h"
#include "web_http.h"
#include "web_pages.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

uint64_t uptimeMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
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
    web_http::jsonEscape(status.lastError, esc_error, sizeof(esc_error));
    std::snprintf(out,
                  out_len,
                  "\"homekit\":{"
                  "\"enabled\":%s,"
                  "\"started\":%s,"
                  "\"paired_controllers\":%d,"
                  "\"accessory_name\":\"%s\","
                  "\"setup_code\":\"%s\","
                  "\"setup_id\":\"%s\","
                  "\"setup_payload\":\"%s\","
                  "\"last_event\":\"%s\","
                  "\"last_error\":\"%s\""
                  "}",
                  status.enabled ? "true" : "false",
                  status.started ? "true" : "false",
                  status.pairedControllers,
                  status.accessoryName,
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
    std::snprintf(body,
                  sizeof(body),
                  "{\"ok\":true,\"device\":\"%s\",\"phase\":\"%s\",\"uptime_ms\":%llu}",
                  app_config::kDeviceName,
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

    char mock_json[768] = {};
    writeMockStateJson(mock, mock_json, sizeof(mock_json));

    char homekit_json[512] = {};
    writeHomeKitJson(homekit, homekit_json, sizeof(homekit_json));

    char esc_transport_err[128] = {};
    web_http::jsonEscape(transport.lastError, esc_transport_err, sizeof(esc_transport_err));
    char esc_log_path[160] = {};
    web_http::jsonEscape(log.currentPath, esc_log_path, sizeof(esc_log_path));

    constexpr size_t kStatusBodyLen = 5120;
    char* body = static_cast<char*>(std::calloc(kStatusBodyLen, sizeof(char)));
    if (body == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response allocation failed");
    }

    const char* transport_mode = app_config::kCn105UseRealTransport ? "real" : "mock";

    std::snprintf(body,
                  kStatusBodyLen,
                  "{"
                  "\"ok\":true,"
                  "\"device\":\"%s\","
                  "\"phase\":\"%s\","
                  "\"uptime_ms\":%llu,"
                  "\"wifi\":{\"initialized\":%s,\"connected\":%s,\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"channel\":%d,\"mac\":\"%s\",\"last_event\":\"%s\",\"last_event_age_ms\":%lu},"
                  "%s,"
                  "\"filesystem\":{\"mounted\":%s,\"base_path\":\"%s\",\"total_bytes\":%u,\"used_bytes\":%u,\"free_bytes\":%u},"
                  "\"log\":{\"active\":%s,\"current\":\"%s\",\"current_bytes\":%u,\"dropped_lines\":%u,\"level\":\"%s\"},"
                  "\"cn105\":{\"uart_initialized\":%s,\"uart\":%d,\"rx_pin\":%d,\"tx_pin\":%d,\"baud\":%d,\"format\":\"%s\",\"transport\":\"%s\","
                  "\"transport_status\":{\"running\":%s,\"connected\":%s,\"phase\":\"%s\",\"connect_attempts\":%lu,\"poll_cycles\":%lu,\"rx_packets\":%lu,\"rx_errors\":%lu,\"tx_packets\":%lu,\"sets_pending\":%lu,\"last_error\":\"%s\"},"
                  "%s}"
                  "}",
                  app_config::kDeviceName,
                  app_config::kPhaseName,
                  static_cast<unsigned long long>(uptimeMs()),
                  wifi.initialized ? "true" : "false",
                  wifi.staConnected ? "true" : "false",
                  wifi.mode,
                  wifi.ip,
                  wifi.rssi,
                  wifi.channel,
                  wifi.mac,
                  wifi.lastEvent,
                  static_cast<unsigned long>(wifi.lastEventAgeMs),
                  homekit_json,
                  fs.mounted ? "true" : "false",
                  platform_fs::basePath(),
                  static_cast<unsigned>(fs.totalBytes),
                  static_cast<unsigned>(fs.usedBytes),
                  static_cast<unsigned>(fs.freeBytes),
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
        if (app_config::kCn105UseRealTransport) {
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
    { "/api/maintenance/reset-homekit", HTTP_POST, resetHomeKitHandler },
    { "/api/maintenance/clear-logs", HTTP_POST, clearLogsHandler },
    { "/api/maintenance/clear-spiffs", HTTP_POST, clearSpiffsHandler },
    { "/api/maintenance/clear-all-nvs", HTTP_POST, clearAllNvsHandler },
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
