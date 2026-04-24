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
#include "esp_http_server.h"

namespace web_routes {

esp_err_t registerRoutes(httpd_handle_t server);

}  // namespace web_routes
