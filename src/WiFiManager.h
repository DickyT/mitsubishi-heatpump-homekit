#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_err.h>
#include <esp_wifi.h>

#include "AppConfig.h"
#include "DebugLog.h"

class WiFiManager {
public:
    void begin() {
        WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t info) {
            (void)info;
            handleEvent(event);
        });
        connect();
    }

    void maintain() {
        if (!hasConfiguredStaCredentials()) {
            return;
        }

        if (WiFi.getMode() != WIFI_STA) {
            return;
        }

        uint32_t now = millis();
        if (WiFi.status() == WL_CONNECTED) {
            return;
        }

        if (now - lastReconnectAttemptMs_ < AppConfig::WIFI_RECONNECT_INTERVAL_MS) {
            return;
        }

        lastReconnectAttemptMs_ = now;
        Serial.printf("[WiFi] Reconnecting at %s (status=%s)\n",
                      DebugLog::formatElapsedTime(now).c_str(),
                      DebugLog::wifiStatusLabel(WiFi.status()));
        WiFi.disconnect(false, false);
        WiFi.begin(AppConfig::WIFI_SSID, AppConfig::WIFI_PASSWORD);
        disableWiFiPowerSave("reconnect");
    }

    void logHeartbeat() {
        uint32_t now = millis();
        if (now - lastHeartbeatMs_ < AppConfig::HEARTBEAT_INTERVAL_MS) {
            return;
        }

        lastHeartbeatMs_ = now;
        String prefix = "[Heartbeat " + DebugLog::formatElapsedTime(now) + "]";
        logStatus(prefix.c_str());
    }

    void logWebAddress() const {
        if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
            Serial.printf("[WiFi] Open http://%s:%u/\n", WiFi.softAPIP().toString().c_str(), AppConfig::WEB_PORT);
            return;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi] Open http://%s:%u/\n", WiFi.localIP().toString().c_str(), AppConfig::WEB_PORT);
        }
    }

    String webHostIp() const {
        if (WiFi.status() == WL_CONNECTED) {
            return WiFi.localIP().toString();
        }
        return WiFi.softAPIP().toString();
    }

private:
    uint32_t lastReconnectAttemptMs_ = 0;
    uint32_t lastHeartbeatMs_ = 0;
    uint32_t lastWiFiEventMs_ = 0;
    String lastWiFiEventName_ = "boot";

    bool hasConfiguredStaCredentials() const {
        return strcmp(AppConfig::WIFI_SSID, "YOUR_WIFI_SSID") != 0 && strlen(AppConfig::WIFI_SSID) > 0;
    }

    String currentBssid() const {
        if (WiFi.status() != WL_CONNECTED) {
            return "--";
        }

        String bssid = WiFi.BSSIDstr();
        if (bssid.length() == 0) {
            return "--";
        }
        return bssid;
    }

    int currentChannel() const {
        if (WiFi.status() != WL_CONNECTED) {
            return 0;
        }
        return WiFi.channel();
    }

    void handleEvent(arduino_event_id_t event) {
        uint32_t now = millis();
        lastWiFiEventMs_ = now;

        switch (event) {
            case ARDUINO_EVENT_WIFI_READY:
                lastWiFiEventName_ = "WIFI_READY";
                break;
            case ARDUINO_EVENT_WIFI_STA_START:
                lastWiFiEventName_ = "STA_START";
                break;
            case ARDUINO_EVENT_WIFI_STA_CONNECTED:
                lastWiFiEventName_ = "STA_CONNECTED";
                break;
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                lastWiFiEventName_ = "STA_DISCONNECTED";
                break;
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                lastWiFiEventName_ = "STA_GOT_IP";
                break;
            case ARDUINO_EVENT_WIFI_STA_LOST_IP:
                lastWiFiEventName_ = "STA_LOST_IP";
                break;
            case ARDUINO_EVENT_WIFI_AP_START:
                lastWiFiEventName_ = "AP_START";
                break;
            case ARDUINO_EVENT_WIFI_AP_STOP:
                lastWiFiEventName_ = "AP_STOP";
                break;
            default:
                lastWiFiEventName_ = "EVENT_" + String(static_cast<int>(event));
                break;
        }

        Serial.printf("[WiFiEvent %s] %s\n",
                      DebugLog::formatElapsedTime(now).c_str(),
                      lastWiFiEventName_.c_str());
    }

    void logStatus(const char* prefix) const {
        uint32_t now = millis();
        uint32_t lastEventAgeMs = lastWiFiEventMs_ == 0 ? 0 : (now - lastWiFiEventMs_);

        Serial.printf("%s status=%d(%s) mode=%d(%s) ip=%s rssi=%d mac=%s channel=%d bssid=%s lastEvent=%s age=%lums\n",
                      prefix,
                      WiFi.status(),
                      DebugLog::wifiStatusLabel(WiFi.status()),
                      WiFi.getMode(),
                      DebugLog::wifiModeLabel(WiFi.getMode()),
                      WiFi.localIP().toString().c_str(),
                      WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
                      WiFi.macAddress().c_str(),
                      currentChannel(),
                      currentBssid().c_str(),
                      lastWiFiEventName_.c_str(),
                      static_cast<unsigned long>(lastEventAgeMs));
    }

    void startFallbackAp() {
        WiFi.disconnect(true, true);
        delay(200);
        WiFi.mode(WIFI_AP);
        disableWiFiPowerSave("fallback-ap");
        WiFi.softAP(AppConfig::FALLBACK_AP_SSID, AppConfig::FALLBACK_AP_PASSWORD);
        Serial.printf("[WiFi] Fallback AP started: %s\n", AppConfig::FALLBACK_AP_SSID);
        Serial.printf("[WiFi] AP password: %s\n", AppConfig::FALLBACK_AP_PASSWORD);
        logWebAddress();
    }

    void connect() {
        WiFi.persistent(false);
        WiFi.setAutoReconnect(true);

        if (!hasConfiguredStaCredentials()) {
            Serial.println("[WiFi] No STA credentials configured, starting fallback AP");
            startFallbackAp();
            return;
        }

        WiFi.mode(WIFI_STA);
        disableWiFiPowerSave("sta-mode");
        WiFi.begin(AppConfig::WIFI_SSID, AppConfig::WIFI_PASSWORD);
        disableWiFiPowerSave("sta-begin");
        Serial.printf("[WiFi] Connecting to %s", AppConfig::WIFI_SSID);

        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < AppConfig::WIFI_CONNECT_TIMEOUT_MS) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi] Connected to %s\n", AppConfig::WIFI_SSID);
            Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
            return;
        }

        Serial.println("[WiFi] STA connection failed, switching to fallback AP");
        startFallbackAp();
    }

    void disableWiFiPowerSave(const char* stage) const {
        if (!AppConfig::WIFI_DISABLE_SLEEP && !AppConfig::WIFI_DISABLE_POWER_SAVE) {
            return;
        }

        WiFi.setSleep(false);
        esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
        if (err == ESP_OK) {
            Serial.printf("[WiFi] Power save disabled: stage=%s sleep=false ps=none\n", stage);
            return;
        }

        Serial.printf("[WiFi] Power save disable failed: stage=%s err=%s\n",
                      stage,
                      esp_err_to_name(err));
    }
};
