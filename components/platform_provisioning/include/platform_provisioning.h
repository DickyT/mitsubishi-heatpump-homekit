#pragma once

#include "esp_err.h"

#include <cstdint>

namespace platform_provisioning {

struct Status {
    bool initialized = false;
    bool active = false;
    bool credentialsReceived = false;
    bool rebootPending = false;
    uint32_t remainingMs = 0;
    int buttonGpio = -1;
    char stage[24] = "idle";
    char lastResult[24] = "idle";
    char serviceName[40] = "--";
    char pendingSsid[33] = "";
};

esp_err_t init();
Status getStatus();

}  // namespace platform_provisioning
