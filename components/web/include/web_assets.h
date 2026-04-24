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
#include <cstdint>

namespace web_assets {

struct GzipAsset {
    const char* path;
    const uint8_t* start;
    const uint8_t* end;
    const char* contentType;
};

const GzipAsset* find(const char* path);
const char* version();

}  // namespace web_assets
