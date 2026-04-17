#pragma once

#include "esp_err.h"

namespace homekit_bridge {

struct Status {
    bool enabled = false;
    bool started = false;
    int pairedControllers = 0;
    const char* accessoryName = "";
    const char* setupCode = "";
    const char* setupId = "";
    const char* setupPayload = "";
    const char* lastEvent = "";
    const char* lastError = "";
};

esp_err_t start();
Status getStatus();
void syncFromMock();

}  // namespace homekit_bridge
