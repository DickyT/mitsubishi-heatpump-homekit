#pragma once

#include "esp_err.h"

#include <cstdint>

namespace platform_wifi {

struct Status {
    bool initialized = false;
    bool staConnected = false;
    bool fallbackApActive = false;
    int rssi = 0;
    int channel = 0;
    char ssid[33] = "";
    char bssid[18] = "--";
    char mode[12] = "OFF";
    char ip[16] = "0.0.0.0";
    char mac[18] = "--";
    char lastEvent[32] = "boot";
    uint32_t lastEventAgeMs = 0;
};

esp_err_t init();
Status getStatus();
void maintain();
void logStatus(const char* prefix);

}  // namespace platform_wifi
