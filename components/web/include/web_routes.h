#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

namespace web_routes {

esp_err_t registerRoutes(httpd_handle_t server);

}  // namespace web_routes
