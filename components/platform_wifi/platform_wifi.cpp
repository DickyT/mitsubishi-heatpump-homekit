#include "platform_wifi.h"

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <cstdio>
#include <cstring>

namespace {

const char* TAG = "platform_wifi";

bool initialized = false;
bool sta_connected = false;
bool sta_mode_active = false;
int reconnect_attempts = 0;
constexpr int kMaxFastReconnects = 3;
int64_t last_event_us = 0;
int64_t last_reconnect_us = 0;
char current_ip[16] = "0.0.0.0";
char last_event[32] = "boot";
esp_netif_t* sta_netif = nullptr;

bool hasConfiguredStaCredentials() {
    return std::strcmp(app_config::kWifiSsid, "YOUR_WIFI_SSID") != 0 && app_config::kWifiSsid[0] != '\0';
}

void setLastEvent(const char* name) {
    std::strncpy(last_event, name, sizeof(last_event) - 1);
    last_event[sizeof(last_event) - 1] = '\0';
    last_event_us = esp_timer_get_time();
    ESP_LOGI(TAG, "event=%s", last_event);
}

void formatMac(char* out, size_t out_len) {
    uint8_t mac[6] = {};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) {
        std::strncpy(out, "--", out_len);
        return;
    }

    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char* modeName(wifi_mode_t mode) {
    switch (mode) {
        case WIFI_MODE_NULL:
            return "OFF";
        case WIFI_MODE_STA:
            return "STA";
        case WIFI_MODE_AP:
            return "AP";
        case WIFI_MODE_APSTA:
            return "AP_STA";
        default:
            return "?";
    }
}

esp_err_t initNvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs_flash_erase failed");
        err = nvs_flash_init();
    }

    return err;
}

void disablePowerSave(const char* stage) {
    if (!app_config::kWifiDisablePowerSave) {
        return;
    }

    const esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi power save disabled: stage=%s ps=none", stage);
        return;
    }

    ESP_LOGW(TAG, "WiFi power save disable failed: stage=%s err=%s", stage, esp_err_to_name(err));
}

void eventHandler(void*, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_WIFI_READY:
                setLastEvent("WIFI_READY");
                break;
            case WIFI_EVENT_STA_START:
                setLastEvent("STA_START");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_STOP:
                setLastEvent("STA_STOP");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                setLastEvent("STA_CONNECTED");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                setLastEvent("STA_DISCONNECTED");
                sta_connected = false;
                std::strncpy(current_ip, "0.0.0.0", sizeof(current_ip));
                if (sta_mode_active && reconnect_attempts < kMaxFastReconnects) {
                    reconnect_attempts++;
                    esp_wifi_connect();
                }
                break;
            case WIFI_EVENT_AP_START:
                setLastEvent("AP_START");
                break;
            case WIFI_EVENT_AP_STOP:
                setLastEvent("AP_STOP");
                break;
            case WIFI_EVENT_HOME_CHANNEL_CHANGE:
                setLastEvent("HOME_CHANNEL_CHANGE");
                break;
            default:
                snprintf(last_event, sizeof(last_event), "WIFI_%ld", static_cast<long>(event_id));
                last_event_us = esp_timer_get_time();
                ESP_LOGI(TAG, "event=%s", last_event);
                break;
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&event->ip_info.ip));
        sta_connected = true;
        reconnect_attempts = 0;
        setLastEvent("STA_GOT_IP");
        ESP_LOGI(TAG, "Connected to %s ip=%s", app_config::kWifiSsid, current_ip);
    }
}

esp_err_t startSta() {
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_config_t sta_config = {};
    std::strncpy(reinterpret_cast<char*>(sta_config.sta.ssid), app_config::kWifiSsid, sizeof(sta_config.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(sta_config.sta.password),
                 app_config::kWifiPassword,
                 sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = std::strlen(app_config::kWifiPassword) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode STA failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "esp_wifi_set_config STA failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start STA failed");
    disablePowerSave("sta-start");

    sta_mode_active = true;
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", app_config::kWifiSsid);
    return ESP_OK;
}

esp_err_t waitForStaConnection() {
    const int64_t deadline_us = esp_timer_get_time() + (static_cast<int64_t>(app_config::kWifiConnectTimeoutMs) * 1000);
    while (!sta_connected && esp_timer_get_time() < deadline_us) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    if (sta_connected) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "STA connection timed out after %lums; staying in STA mode and continuing reconnect attempts",
             static_cast<unsigned long>(app_config::kWifiConnectTimeoutMs));
    return ESP_OK;
}

}  // namespace

namespace platform_wifi {

esp_err_t init() {
    if (initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(initNvs(), TAG, "NVS init failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    const wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_config), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "esp_wifi_set_storage failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, nullptr, nullptr),
                        TAG,
                        "register WIFI_EVENT handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventHandler, nullptr, nullptr),
                        TAG,
                        "register IP_EVENT handler failed");

    initialized = true;

    if (!hasConfiguredStaCredentials()) {
        ESP_LOGW(TAG, "No STA WiFi credentials configured; WiFi will remain offline and no fallback AP will be started");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(startSta(), TAG, "start STA failed");
    return waitForStaConnection();
}

Status getStatus() {
    Status status{};
    status.initialized = initialized;
    status.staConnected = sta_connected;
    status.fallbackApActive = false;

    wifi_mode_t mode = WIFI_MODE_NULL;
    if (initialized && esp_wifi_get_mode(&mode) == ESP_OK) {
        std::strncpy(status.mode, modeName(mode), sizeof(status.mode) - 1);
    }

    std::strncpy(status.ip, current_ip, sizeof(status.ip) - 1);
    formatMac(status.mac, sizeof(status.mac));
    std::strncpy(status.lastEvent, last_event, sizeof(status.lastEvent) - 1);

    if (last_event_us > 0) {
        status.lastEventAgeMs = static_cast<uint32_t>((esp_timer_get_time() - last_event_us) / 1000);
    }

    if (sta_connected) {
        wifi_ap_record_t ap_info = {};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            status.rssi = ap_info.rssi;
            status.channel = ap_info.primary;
        }
    }

    return status;
}

void maintain() {
    if (!initialized || !sta_mode_active || sta_connected) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_reconnect_us < static_cast<int64_t>(app_config::kWifiReconnectIntervalMs) * 1000) {
        return;
    }

    last_reconnect_us = now_us;
    reconnect_attempts = 0;
    ESP_LOGW(TAG, "Reconnecting WiFi SSID: %s", app_config::kWifiSsid);
    esp_wifi_connect();
}

void logStatus(const char* prefix) {
    const Status status = getStatus();
    ESP_LOGI(TAG,
             "%s initialized=%s connected=%s fallbackAp=%s mode=%s ip=%s rssi=%d channel=%d mac=%s lastEvent=%s age=%lums",
             prefix,
             status.initialized ? "yes" : "no",
             status.staConnected ? "yes" : "no",
             status.fallbackApActive ? "yes" : "no",
             status.mode,
             status.ip,
             status.rssi,
             status.channel,
             status.mac,
             status.lastEvent,
             static_cast<unsigned long>(status.lastEventAgeMs));
}

}  // namespace platform_wifi
