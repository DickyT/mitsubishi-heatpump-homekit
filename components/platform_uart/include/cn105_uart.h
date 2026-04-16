#pragma once

#include "esp_err.h"

namespace cn105_uart {

struct Status {
    bool initialized = false;
    int uart = 0;
    int rxPin = 0;
    int txPin = 0;
    int baudRate = 0;
    const char* format = "8E1";
};

esp_err_t init();
Status getStatus();

}  // namespace cn105_uart
