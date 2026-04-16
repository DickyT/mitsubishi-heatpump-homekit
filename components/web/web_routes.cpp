#include "web_routes.h"

#include "app_config.h"
#include "cn105_core.h"
#include "cn105_uart.h"
#include "esp_timer.h"
#include "platform_fs.h"
#include "platform_wifi.h"
#include "web_http.h"
#include "web_pages.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

uint64_t uptimeMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

void writeMockStateJson(const cn105_core::MockState& state, char* out, size_t out_len) {
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
                  state.lastError);
}

esp_err_t rootHandler(httpd_req_t* req) {
    constexpr size_t kRootBodyLen = 4096;
    char* body = static_cast<char*>(std::calloc(kRootBodyLen, sizeof(char)));
    if (body == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response allocation failed");
    }

    if (!web_pages::renderRoot(body, kRootBodyLen)) {
        std::free(body);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "page render failed");
    }

    const esp_err_t err = web_http::sendText(req, "text/html; charset=utf-8", body);
    std::free(body);
    return err;
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

    char mock_json[768] = {};
    writeMockStateJson(mock, mock_json, sizeof(mock_json));

    constexpr size_t kStatusBodyLen = 3072;
    char* body = static_cast<char*>(std::calloc(kStatusBodyLen, sizeof(char)));
    if (body == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response allocation failed");
    }

    std::snprintf(body,
                  kStatusBodyLen,
                  "{"
                  "\"ok\":true,"
                  "\"device\":\"%s\","
                  "\"phase\":\"%s\","
                  "\"uptime_ms\":%llu,"
                  "\"wifi\":{\"initialized\":%s,\"connected\":%s,\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"channel\":%d,\"mac\":\"%s\",\"last_event\":\"%s\",\"last_event_age_ms\":%lu},"
                  "\"filesystem\":{\"mounted\":%s,\"base_path\":\"%s\",\"total_bytes\":%u,\"used_bytes\":%u,\"free_bytes\":%u},"
                  "\"cn105\":{\"uart_initialized\":%s,\"uart\":%d,\"rx_pin\":%d,\"tx_pin\":%d,\"baud\":%d,\"format\":\"%s\",\"transport\":\"mock\",%s}"
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
                  fs.mounted ? "true" : "false",
                  platform_fs::basePath(),
                  static_cast<unsigned>(fs.totalBytes),
                  static_cast<unsigned>(fs.usedBytes),
                  static_cast<unsigned>(fs.freeBytes),
                  cn105.initialized ? "true" : "false",
                  cn105.uart,
                  cn105.rxPin,
                  cn105.txPin,
                  cn105.baudRate,
                  cn105.format,
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
    char value[48] = {};
    bool any = false;

    if (web_http::queryValue(query, "power", value, sizeof(value))) {
        command.hasPower = true;
        command.power = value;
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
    const bool apply = web_http::queryValue(query, "apply", apply_value, sizeof(apply_value)) && std::strcmp(apply_value, "1") == 0;
    if (apply && !cn105_core::applySetPacketToMock(packet.bytes, packet.length, error, sizeof(error))) {
        return web_http::sendJsonError(req, error);
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

const web_http::Route ROUTES[] = {
    { "/", HTTP_GET, rootHandler },
    { "/api/health", HTTP_GET, healthHandler },
    { "/api/status", HTTP_GET, statusHandler },
    { "/api/cn105/mock/status", HTTP_GET, cn105MockStatusHandler },
    { "/api/cn105/mock/build-set", HTTP_GET, cn105BuildSetHandler },
    { "/api/cn105/decode", HTTP_GET, cn105DecodeHandler },
};

}  // namespace

namespace web_routes {

esp_err_t registerRoutes(httpd_handle_t server) {
    return web_http::registerRoutes(server, ROUTES, sizeof(ROUTES) / sizeof(ROUTES[0]));
}

}  // namespace web_routes
