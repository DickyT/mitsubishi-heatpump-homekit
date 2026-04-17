#include "web_server.h"

#include "app_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "platform_wifi.h"
#include "web_routes.h"

namespace {

const char* TAG = "web_server";
httpd_handle_t server = nullptr;

}  // namespace

namespace web_server {

esp_err_t start() {
    if (server != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = app_config::kWebServerPort;
    config.stack_size = app_config::kWebServerStackBytes;
    config.max_open_sockets = app_config::kWebServerMaxOpenSockets;
    config.max_uri_handlers = app_config::kWebServerMaxUriHandlers;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG,
             "Starting WebUI on port %u stack=%u max_open_sockets=%u max_uri_handlers=%u",
             static_cast<unsigned>(config.server_port),
             static_cast<unsigned>(config.stack_size),
             static_cast<unsigned>(config.max_open_sockets),
             static_cast<unsigned>(config.max_uri_handlers));
    const esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_err_t route_err = web_routes::registerRoutes(server);
    if (route_err != ESP_OK) {
        ESP_LOGE(TAG, "route registration failed: %s", esp_err_to_name(route_err));
        httpd_stop(server);
        server = nullptr;
        return route_err;
    }

    ESP_LOGI(TAG, "WebUI ready: http://%s:%u/", platform_wifi::getStatus().ip, static_cast<unsigned>(config.server_port));
    return ESP_OK;
}

}  // namespace web_server
