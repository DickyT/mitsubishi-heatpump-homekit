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

#include "esp_http_server.h"

namespace web_pages {

esp_err_t sendRoot(httpd_req_t* req);
esp_err_t sendLogs(httpd_req_t* req);
esp_err_t sendAdmin(httpd_req_t* req);
esp_err_t sendAsset(httpd_req_t* req);

}  // namespace web_pages
