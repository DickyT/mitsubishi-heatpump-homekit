#pragma once

#include "esp_http_server.h"

namespace web_pages {

esp_err_t sendRoot(httpd_req_t* req);
esp_err_t sendDebug(httpd_req_t* req);
esp_err_t sendAdmin(httpd_req_t* req);

}  // namespace web_pages
