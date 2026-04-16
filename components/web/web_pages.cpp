#include "web_pages.h"

#include "app_config.h"
#include "cn105_core.h"
#include "cn105_uart.h"
#include "esp_timer.h"
#include "platform_fs.h"
#include "platform_wifi.h"

#include <cstdio>

namespace {

uint64_t uptimeMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

}  // namespace

namespace web_pages {

bool renderRoot(char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return false;
    }

    const platform_wifi::Status wifi = platform_wifi::getStatus();
    const platform_fs::Status fs = platform_fs::getStatus();
    const cn105_uart::Status cn105 = cn105_uart::getStatus();
    const cn105_core::MockState mock = cn105_core::getMockState();

    const int written = std::snprintf(
        out,
        out_len,
        "<!doctype html>"
        "<html lang=\"zh-Hans\">"
        "<head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>%s</title>"
        "<style>"
        ":root{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#17211a;background:#f4f0e6;}"
        "body{margin:0;padding:28px;}"
        "main{max-width:820px;margin:0 auto;background:#fffaf0;border:1px solid #dfd3bd;border-radius:22px;padding:24px;box-shadow:0 20px 60px rgba(66,52,30,.13);}"
        "h1{margin:0 0 6px;font-size:30px;}"
        "p{line-height:1.55;}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px;margin-top:20px;}"
        ".card{background:#fdf6e8;border:1px solid #eadcc5;border-radius:16px;padding:16px;}"
        ".label{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:#7d725f;}"
        ".value{font-size:18px;font-weight:700;margin-top:6px;word-break:break-word;}"
        "code{background:#efe3ce;border-radius:8px;padding:3px 7px;}"
        "a{color:#795c20;font-weight:700;}"
        "</style>"
        "</head>"
        "<body><main>"
        "<h1>三菱空调桥接器</h1>"
        "<p>当前是 M5 离线 CN105 协议层：真实串口控制还没启用，但 SET payload builder 和 mock state 已经可以验证。</p>"
        "<div class=\"grid\">"
        "<section class=\"card\"><div class=\"label\">设备</div><div class=\"value\">%s</div></section>"
        "<section class=\"card\"><div class=\"label\">运行时间</div><div class=\"value\">%llu ms</div></section>"
        "<section class=\"card\"><div class=\"label\">Wi-Fi</div><div class=\"value\">%s / %s</div><p>IP: <code>%s</code><br>RSSI: %d dBm<br>Last: %s</p></section>"
        "<section class=\"card\"><div class=\"label\">SPIFFS</div><div class=\"value\">%s</div><p>Used: %u / %u bytes</p></section>"
        "<section class=\"card\"><div class=\"label\">CN105 UART</div><div class=\"value\">%s</div><p>UART%d RX=%d TX=%d %d %s</p></section>"
        "<section class=\"card\"><div class=\"label\">CN105 Mock</div><div class=\"value\">%s %d°F</div><p>power=%s fan=%s vane=%s wide=%s</p></section>"
        "<section class=\"card\"><div class=\"label\">API</div><p><a href=\"/api/health\">/api/health</a><br><a href=\"/api/status\">/api/status</a><br><a href=\"/api/cn105/mock/status\">/api/cn105/mock/status</a></p></section>"
        "</div>"
        "</main></body></html>",
        app_config::kDeviceName,
        app_config::kDeviceName,
        static_cast<unsigned long long>(uptimeMs()),
        wifi.mode,
        wifi.staConnected ? "connected" : "offline",
        wifi.ip,
        wifi.rssi,
        wifi.lastEvent,
        fs.mounted ? "mounted" : "unavailable",
        static_cast<unsigned>(fs.usedBytes),
        static_cast<unsigned>(fs.totalBytes),
        cn105.initialized ? "ready" : "not ready",
        cn105.uart,
        cn105.rxPin,
        cn105.txPin,
        cn105.baudRate,
        cn105.format,
        mock.mode,
        mock.targetTemperatureF,
        mock.power,
        mock.fan,
        mock.vane,
        mock.wideVane);

    return written > 0 && static_cast<size_t>(written) < out_len;
}

}  // namespace web_pages
