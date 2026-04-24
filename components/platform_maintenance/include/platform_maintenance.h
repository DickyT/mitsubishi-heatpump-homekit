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

#include <cstddef>

namespace platform_maintenance {

struct Result {
    bool ok = false;
    bool rebooting = false;
    char action[40] = "";
    char message[512] = "";
};

Result resetHomeKit();
Result clearLogs();
Result clearSpiffs();
Result clearAllNvs();
void rebootSoon();

}  // namespace platform_maintenance
