#include "web_pages.h"

#include "esp_http_server.h"

extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[]   asm("_binary_style_css_end");
extern const char tabs_html_start[] asm("_binary_tabs_html_start");
extern const char tabs_html_end[]   asm("_binary_tabs_html_end");
extern const char root_html_start[] asm("_binary_root_html_start");
extern const char root_html_end[]   asm("_binary_root_html_end");
extern const char root_js_start[]   asm("_binary_root_js_start");
extern const char root_js_end[]     asm("_binary_root_js_end");
extern const char debug_html_start[] asm("_binary_debug_html_start");
extern const char debug_html_end[]   asm("_binary_debug_html_end");
extern const char debug_js_start[]   asm("_binary_debug_js_start");
extern const char debug_js_end[]     asm("_binary_debug_js_end");
extern const char admin_html_start[] asm("_binary_admin_html_start");
extern const char admin_html_end[]   asm("_binary_admin_html_end");
extern const char admin_js_start[]   asm("_binary_admin_js_start");
extern const char admin_js_end[]     asm("_binary_admin_js_end");

namespace {

const char* kHtmlHead = "<!doctype html><html lang=\"zh-Hans\"><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
    "<meta name=\"apple-mobile-web-app-capable\" content=\"yes\">"
    "<title>Mitsubishi AC</title><style>";
const char* kHeadEnd = "</style></head><body>";
const char* kScriptOpen = "<script>";
const char* kHtmlEnd = "</script></body></html>";

size_t embeddedLen(const char* start, const char* end) {
    size_t len = static_cast<size_t>(end - start);
    if (len > 0 && start[len - 1] == '\0') {
        len--;
    }
    return len;
}

esp_err_t sendPage(httpd_req_t* req, const char* body_start, const char* body_end,
                   const char* js_start, const char* js_end) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_send_chunk(req, kHtmlHead, -1);
    httpd_resp_send_chunk(req, style_css_start, embeddedLen(style_css_start, style_css_end));
    httpd_resp_send_chunk(req, kHeadEnd, -1);
    httpd_resp_send_chunk(req, tabs_html_start, embeddedLen(tabs_html_start, tabs_html_end));
    httpd_resp_send_chunk(req, body_start, embeddedLen(body_start, body_end));
    httpd_resp_send_chunk(req, kScriptOpen, -1);
    httpd_resp_send_chunk(req, js_start, embeddedLen(js_start, js_end));
    httpd_resp_send_chunk(req, kHtmlEnd, -1);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

}  // namespace

namespace web_pages {

esp_err_t sendRoot(httpd_req_t* req) {
    return sendPage(req, root_html_start, root_html_end, root_js_start, root_js_end);
}

esp_err_t sendDebug(httpd_req_t* req) {
    return sendPage(req, debug_html_start, debug_html_end, debug_js_start, debug_js_end);
}

esp_err_t sendAdmin(httpd_req_t* req) {
    return sendPage(req, admin_html_start, admin_html_end, admin_js_start, admin_js_end);
}

}  // namespace web_pages
