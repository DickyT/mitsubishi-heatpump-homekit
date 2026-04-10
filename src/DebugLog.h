#pragma once

#include <Arduino.h>
#include <WiFi.h>

class DebugLog {
public:
    static String formatElapsedTime(uint32_t ms) {
        uint32_t totalSeconds = ms / 1000;
        uint32_t days = totalSeconds / 86400;
        uint32_t hours = (totalSeconds % 86400) / 3600;
        uint32_t minutes = (totalSeconds % 3600) / 60;
        uint32_t seconds = totalSeconds % 60;

        char buffer[24];
        snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu",
                 static_cast<unsigned long>(days),
                 static_cast<unsigned long>(hours),
                 static_cast<unsigned long>(minutes),
                 static_cast<unsigned long>(seconds));
        return String(buffer);
    }

    static const char* wifiStatusLabel(wl_status_t status) {
        switch (status) {
            case WL_IDLE_STATUS: return "IDLE";
            case WL_NO_SSID_AVAIL: return "NO_SSID";
            case WL_SCAN_COMPLETED: return "SCAN_DONE";
            case WL_CONNECTED: return "CONNECTED";
            case WL_CONNECT_FAILED: return "CONNECT_FAILED";
            case WL_CONNECTION_LOST: return "CONNECTION_LOST";
            case WL_DISCONNECTED: return "DISCONNECTED";
            default: return "UNKNOWN";
        }
    }

    static const char* wifiModeLabel(wifi_mode_t mode) {
        switch (mode) {
            case WIFI_OFF: return "OFF";
            case WIFI_STA: return "STA";
            case WIFI_AP: return "AP";
            case WIFI_AP_STA: return "AP+STA";
            default: return "UNKNOWN";
        }
    }
};
