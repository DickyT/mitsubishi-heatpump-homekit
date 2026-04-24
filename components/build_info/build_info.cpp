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

#include "build_info.h"

#if __has_include("build_info_generated.h")
#include "build_info_generated.h"
#else
#define BUILD_INFO_FIRMWARE_VERSION "dev"
#endif

namespace build_info {

const char* firmwareVersion() {
    return BUILD_INFO_FIRMWARE_VERSION;
}

}  // namespace build_info
