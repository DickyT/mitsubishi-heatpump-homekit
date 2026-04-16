#include "web_server.h"

#include "app_config.h"
#include "cn105_uart.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "platform_fs.h"
#include "platform_wifi.h"

#include <cstdio>
#include <cstdlib>

namespace {

const char* TAG = "web_server";
httpd_handle_t server = nullptr;

uint64_t uptimeMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

esp_err_t sendText(httpd_req_t* req, const char* content_type, const char* body) {
    httpd_resp_set_type(req, content_type);
    return httpd_resp_sendstr(req, body);
}

esp_err_t rootHandler(httpd_req_t* req) {
    const platform_wifi::Status wifi = platform_wifi::getStatus();
    const platform_fs::Status fs = platform_fs::getStatus();
    const cn105_uart::Status cn105 = cn105_uart::getStatus();

    constexpr size_t kRootBodyLen = 4096;
    char* body = static_cast<char*>(std::calloc(kRootBodyLen, sizeof(char)));
    if (body == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response allocation failed");
    }

    snprintf(body,
             kRootBodyLen,
             "<!doctype html>"
             "<html lang=\"zh-Hans\">"
             "<head>"
             "<meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>%s</title>"
             "<style>"
             ":root{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#17211a;background:#f4f0e6;}"
             "body{margin:0;padding:28px;}"
             "main{max-width:820px;margin:0 auto;background:#fffaf0;border:1px solid #dfd3bd;border-radius:22px;padding:24px;box-shadow:0 20px 60px rgba(66,52,30,.13);}"
             "h1{margin:0 0 6px;font-size:30px;}"
             "p{line-height:1.55;}"
             ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px;margin-top:20px;}"
             ".card{background:#fdf6e8;border:1px solid #eadcc5;border-radius:16px;padding:16px;}"
             ".label{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:#7d725f;}"
             ".value{font-size:18px;font-weight:700;margin-top:6px;word-break:break-word;}"
             "code{background:#efe3ce;border-radius:8px;padding:3px 7px;}"
             "a{color:#795c20;font-weight:700;}"
             "</style>"
             "</head>"
             "<body><main>"
             "<h1>三菱空调桥接器</h1>"
             "<p>当前是 M4 最小 WebUI：先验证 HTTP 通路和平台状态，CN105 协议与 HomeKit 还没有恢复。</p>"
             "<div class=\"grid\">"
             "<section class=\"card\"><div class=\"label\">设备</div><div class=\"value\">%s</div></section>"
             "<section class=\"card\"><div class=\"label\">运行时间</div><div class=\"value\">%llu ms</div></section>"
             "<section class=\"card\"><div class=\"label\">Wi-Fi</div><div class=\"value\">%s / %s</div><p>IP: <code>%s</code><br>RSSI: %d dBm<br>Last: %s</p></section>"
             "<section class=\"card\"><div class=\"label\">SPIFFS</div><div class=\"value\">%s</div><p>Used: %u / %u bytes</p></section>"
             "<section class=\"card\"><div class=\"label\">CN105 UART</div><div class=\"value\">%s</div><p>UART%d RX=%d TX=%d %d %s</p></section>"
             "<section class=\"card\"><div class=\"label\">API</div><div class=\"value\"><a href=\"/api/health\">/api/health</a></div><p><a href=\"/api/status\">/api/status</a></p></section>"
             "</div>"
             "</main></body></html>",
             app_config::kDeviceName,
             app_config::kDeviceName,
             static_cast<unsigned long long>(uptimeMs()),
             wifi.mode,
             wifi.staConnected ? "connected" : "offline",
             wifi.ip,
             wifi.rssi,
             wifi.lastEvent,
             fs.mounted ? "mounted" : "unavailable",
             static_cast<unsigned>(fs.usedBytes),
             static_cast<unsigned>(fs.totalBytes),
             cn105.initialized ? "ready" : "not ready",
             cn105.uart,
             cn105.rxPin,
             cn105.txPin,
             cn105.baudRate,
             cn105.format);

    const esp_err_t err = sendText(req, "text/html; charset=utf-8", body);
    std::free(body);
    return err;
}

esp_err_t healthHandler(httpd_req_t* req) {
    char body[512] = {};
    snprintf(body,
             sizeof(body),
             "{\"ok\":true,\"device\":\"%s\",\"phase\":\"%s\",\"uptime_ms\":%llu}",
             app_config::kDeviceName,
             app_config::kPhaseName,
             static_cast<unsigned long long>(uptimeMs()));

    return sendText(req, "application/json", body);
}

esp_err_t statusHandler(httpd_req_t* req) {
    const platform_wifi::Status wifi = platform_wifi::getStatus();
    const platform_fs::Status fs = platform_fs::getStatus();
    const cn105_uart::Status cn105 = cn105_uart::getStatus();

    constexpr size_t kStatusBodyLen = 2048;
    char* body = static_cast<char*>(std::calloc(kStatusBodyLen, sizeof(char)));
    if (body == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response allocation failed");
    }

    snprintf(body,
             kStatusBodyLen,
             "{"
             "\"ok\":true,"
             "\"device\":\"%s\","
             "\"phase\":\"%s\","
             "\"uptime_ms\":%llu,"
             "\"wifi\":{\"initialized\":%s,\"connected\":%s,\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"channel\":%d,\"mac\":\"%s\",\"last_event\":\"%s\",\"last_event_age_ms\":%lu},"
             "\"filesystem\":{\"mounted\":%s,\"base_path\":\"%s\",\"total_bytes\":%u,\"used_bytes\":%u,\"free_bytes\":%u},"
             "\"cn105\":{\"uart_initialized\":%s,\"uart\":%d,\"rx_pin\":%d,\"tx_pin\":%d,\"baud\":%d,\"format\":\"%s\",\"transport\":\"not_restored\"}"
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
             cn105.format);

    const esp_err_t err = sendText(req, "application/json", body);
    std::free(body);
    return err;
}

void registerUriHandlers() {
    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = rootHandler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &root);

    const httpd_uri_t health = {
        .uri = "/api/health",
        .method = HTTP_GET,
        .handler = healthHandler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &health);

    const httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = statusHandler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &status);
}

}  // namespace

namespace web_server {

esp_err_t start() {
    if (server != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = app_config::kWebServerPort;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting WebUI on port %u", static_cast<unsigned>(config.server_port));
    const esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    registerUriHandlers();
    ESP_LOGI(TAG, "WebUI ready: http://%s:%u/", platform_wifi::getStatus().ip, static_cast<unsigned>(config.server_port));
    return ESP_OK;
}

}  // namespace web_server
