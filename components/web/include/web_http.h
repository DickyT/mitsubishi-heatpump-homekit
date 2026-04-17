#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#include <cstddef>

namespace web_http {

struct Route {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t* req);
};

esp_err_t sendText(httpd_req_t* req, const char* content_type, const char* body);
esp_err_t sendJsonError(httpd_req_t* req, const char* error);
size_t jsonEscape(const char* src, char* out, size_t out_len);

bool readQuery(httpd_req_t* req, char* query, size_t query_len);
bool queryValue(const char* query, const char* key, char* value, size_t value_len);

esp_err_t registerRoutes(httpd_handle_t server, const Route* routes, size_t route_count);

}  // namespace web_http
