#include "web_pages.h"

#include "esp_http_server.h"
#include "web_assets.h"

#include <cstdio>
#include <cstring>

namespace {

esp_err_t sendShell(httpd_req_t* req, const char* page) {
    char body[768] = {};
    std::snprintf(body,
                  sizeof(body),
                  "<!doctype html><html lang=\"zh-Hans\"><head>"
                  "<meta charset=\"utf-8\">"
                  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover\">"
                  "<meta name=\"apple-mobile-web-app-capable\" content=\"yes\">"
                  "<title>Mitsubishi AC</title>"
                  "</head><body data-page=\"%s\">"
                  "<div id=\"app\"><main><h1>加载中</h1><div class=\"subtitle\">WebUI 正在加载...</div></main></div>"
                  "<script src=\"/assets/loader.js?v=%s\" defer></script>"
                  "</body></html>",
                  page,
                  web_assets::version());

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

void stripQuery(const char* uri, char* out, size_t out_len) {
    if (out_len == 0) {
        return;
    }

    size_t written = 0;
    while (uri != nullptr && uri[written] != '\0' && uri[written] != '?' && written + 1 < out_len) {
        out[written] = uri[written];
        written++;
    }
    out[written] = '\0';
}

}  // namespace

namespace web_pages {

esp_err_t sendRoot(httpd_req_t* req) {
    return sendShell(req, "root");
}

esp_err_t sendLogs(httpd_req_t* req) {
    return sendShell(req, "logs");
}

esp_err_t sendAdmin(httpd_req_t* req) {
    return sendShell(req, "admin");
}

esp_err_t sendAsset(httpd_req_t* req) {
    char path[160] = {};
    stripQuery(req->uri, path, sizeof(path));

    const web_assets::GzipAsset* asset = web_assets::find(path);
    if (asset == nullptr) {
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_send(req, "Asset not found", HTTPD_RESP_USE_STRLEN);
    }

    const size_t len = static_cast<size_t>(asset->end - asset->start);
    httpd_resp_set_type(req, asset->contentType);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600, immutable");
    return httpd_resp_send(req, reinterpret_cast<const char*>(asset->start), len);
}

}  // namespace web_pages
