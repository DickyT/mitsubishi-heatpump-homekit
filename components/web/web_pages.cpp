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

#include "web_pages.h"

#include "build_info.h"
#include "esp_http_server.h"
#include "web_assets.h"

#include <cstdio>
#include <cstring>

namespace {

esp_err_t sendShell(httpd_req_t* req, const char* page) {
    char body[2048] = {};
    std::snprintf(body,
                  sizeof(body),
                  "<!doctype html><html lang=\"en\"><head>"
                  "<meta charset=\"utf-8\">"
                  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover\">"
                  "<meta name=\"apple-mobile-web-app-capable\" content=\"yes\">"
                  "<title>Kiri Bridge</title>"
                  "<style>"
                  ":root{color-scheme:dark;background:#050505}"
                  "html,body,#app{min-height:100%%;margin:0}"
                  "body{background:#050505;color:#ff774a;font-family:ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}"
                  "#app{min-height:100vh;display:grid;place-items:center}"
                  ".boot-loader{color:#ff774a;font-size:28px;line-height:1.1;font-weight:400;letter-spacing:.02em}"
                  ".boot-loader-subtitle{margin-top:10px;color:#777;font-size:12px;line-height:1.2;letter-spacing:.02em}"
                  "</style>"
                  "</head><body data-page=\"%s\">"
                  "<div id=\"app\"><div aria-live=\"polite\"><div class=\"boot-loader\">Loading...</div><div class=\"boot-loader-subtitle\">Kiri Bridge / v%s</div></div></div>"
                  "<script src=\"/assets/loader.js?v=%s\" defer></script>"
                  "</body></html>",
                  page,
                  build_info::firmwareVersion(),
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
