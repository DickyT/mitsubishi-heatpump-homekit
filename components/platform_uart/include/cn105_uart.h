/****************************************************************************
 * Kiri Bridge
 * CN105 HomeKit controller for Mitsubishi heat pumps
 * https://kiri.dkt.moe
 * https://github.com/DickyT/kiri-homekit
 *
 * Copyright (c) 2026
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 ****************************************************************************/

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
