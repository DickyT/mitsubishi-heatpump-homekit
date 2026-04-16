#include "web_http.h"

#include <cstdio>

namespace {

int hexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void urlDecodeInPlace(char* value, size_t value_len) {
    if (value == nullptr || value_len == 0) {
        return;
    }

    size_t out = 0;
    for (size_t in = 0; value[in] != '\0' && out < value_len - 1; ++in) {
        if (value[in] == '%' && value[in + 1] != '\0' && value[in + 2] != '\0') {
            const int high = hexNibble(value[in + 1]);
            const int low = hexNibble(value[in + 2]);
            if (high >= 0 && low >= 0) {
                value[out++] = static_cast<char>((high << 4) | low);
                in += 2;
                continue;
            }
        }
        value[out++] = value[in] == '+' ? ' ' : value[in];
    }
    value[out] = '\0';
}

}  // namespace

namespace web_http {

esp_err_t sendText(httpd_req_t* req, const char* content_type, const char* body) {
    httpd_resp_set_type(req, content_type);
    return httpd_resp_sendstr(req, body);
}

esp_err_t sendJsonError(httpd_req_t* req, const char* error) {
    char body[192] = {};
    std::snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", error == nullptr ? "unknown error" : error);
    httpd_resp_set_status(req, "400 Bad Request");
    return sendText(req, "application/json", body);
}

bool readQuery(httpd_req_t* req, char* query, size_t query_len) {
    if (query == nullptr || query_len == 0) {
        return false;
    }
    query[0] = '\0';
    return httpd_req_get_url_query_str(req, query, query_len) == ESP_OK;
}

bool queryValue(const char* query, const char* key, char* value, size_t value_len) {
    if (query == nullptr || query[0] == '\0') {
        return false;
    }
    if (httpd_query_key_value(query, key, value, value_len) != ESP_OK) {
        return false;
    }

    urlDecodeInPlace(value, value_len);
    return true;
}

esp_err_t registerRoutes(httpd_handle_t server, const Route* routes, size_t route_count) {
    if (server == nullptr || routes == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < route_count; ++i) {
        const httpd_uri_t route = {
            .uri = routes[i].uri,
            .method = routes[i].method,
            .handler = routes[i].handler,
            .user_ctx = nullptr,
        };

        const esp_err_t err = httpd_register_uri_handler(server, &route);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

}  // namespace web_http
