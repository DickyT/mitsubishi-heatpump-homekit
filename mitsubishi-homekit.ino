#include <WiFi.h>

#include "src/AppConfig.h"
#include "src/MitsubishiProtocol.h"
#include "src/WebUI.h"

HardwareSerial g_cn105Serial(AppConfig::CN105_UART_PORT);
MitsubishiProtocol g_proto(&g_cn105Serial);
WebUI* g_webUI = nullptr;
uint32_t g_lastReconnectAttemptMs = 0;
uint32_t g_lastHeartbeatMs = 0;
uint32_t g_lastWiFiEventMs = 0;
String g_lastWiFiEventName = "boot";

static String formatElapsedTime(uint32_t ms) {
    uint32_t totalSeconds = ms / 1000;
    uint32_t days = totalSeconds / 86400;
    uint32_t hours = (totalSeconds % 86400) / 3600;
    uint32_t minutes = (totalSeconds % 3600) / 60;
    uint32_t seconds = totalSeconds % 60;

    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu",
             static_cast<unsigned long>(days),
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
    return String(buffer);
}

static const char* wifiStatusLabel(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "IDLE";
        case WL_NO_SSID_AVAIL: return "NO_SSID";
        case WL_SCAN_COMPLETED: return "SCAN_DONE";
        case WL_CONNECTED: return "CONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        default: return "UNKNOWN";
    }
}

static const char* wifiModeLabel(wifi_mode_t mode) {
    switch (mode) {
        case WIFI_OFF: return "OFF";
        case WIFI_STA: return "STA";
        case WIFI_AP: return "AP";
        case WIFI_AP_STA: return "AP+STA";
        default: return "UNKNOWN";
    }
}

static String currentBssid() {
    if (WiFi.status() != WL_CONNECTED) {
        return "--";
    }

    String bssid = WiFi.BSSIDstr();
    if (bssid.length() == 0) {
        return "--";
    }
    return bssid;
}

static int currentChannel() {
    if (WiFi.status() != WL_CONNECTED) {
        return 0;
    }
    return WiFi.channel();
}

static void handleWiFiEvent(WiFiEvent_t event) {
    uint32_t now = millis();
    g_lastWiFiEventMs = now;

    switch (event) {
        case ARDUINO_EVENT_WIFI_READY:
            g_lastWiFiEventName = "WIFI_READY";
            break;
        case ARDUINO_EVENT_WIFI_STA_START:
            g_lastWiFiEventName = "STA_START";
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            g_lastWiFiEventName = "STA_CONNECTED";
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            g_lastWiFiEventName = "STA_DISCONNECTED";
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            g_lastWiFiEventName = "STA_GOT_IP";
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            g_lastWiFiEventName = "STA_LOST_IP";
            break;
        case ARDUINO_EVENT_WIFI_AP_START:
            g_lastWiFiEventName = "AP_START";
            break;
        case ARDUINO_EVENT_WIFI_AP_STOP:
            g_lastWiFiEventName = "AP_STOP";
            break;
        default:
            g_lastWiFiEventName = "EVENT_" + String(static_cast<int>(event));
            break;
    }

    Serial.printf("[WiFiEvent %s] %s\n", formatElapsedTime(now).c_str(), g_lastWiFiEventName.c_str());
}

static bool hasConfiguredStaCredentials() {
    return strcmp(AppConfig::WIFI_SSID, "YOUR_WIFI_SSID") != 0 && strlen(AppConfig::WIFI_SSID) > 0;
}

static void logWifiStatus(const char* prefix) {
    uint32_t now = millis();
    uint32_t lastEventAgeMs = g_lastWiFiEventMs == 0 ? 0 : (now - g_lastWiFiEventMs);

    Serial.printf("%s status=%d(%s) mode=%d(%s) ip=%s rssi=%d mac=%s channel=%d bssid=%s lastEvent=%s age=%lums\n",
                  prefix,
                  WiFi.status(),
                  wifiStatusLabel(WiFi.status()),
                  WiFi.getMode(),
                  wifiModeLabel(WiFi.getMode()),
                  WiFi.localIP().toString().c_str(),
                  WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
                  WiFi.macAddress().c_str(),
                  currentChannel(),
                  currentBssid().c_str(),
                  g_lastWiFiEventName.c_str(),
                  static_cast<unsigned long>(lastEventAgeMs));
}

static void logWebAddress() {
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        Serial.printf("[WiFi] Open http://%s:%u/\n", WiFi.softAPIP().toString().c_str(), AppConfig::WEB_PORT);
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Open http://%s:%u/\n", WiFi.localIP().toString().c_str(), AppConfig::WEB_PORT);
    }
}

static void startFallbackAp() {
    WiFi.disconnect(true, true);
    delay(200);
    WiFi.mode(WIFI_AP);
    if (AppConfig::WIFI_DISABLE_SLEEP) {
        WiFi.setSleep(false);
    }
    WiFi.softAP(AppConfig::FALLBACK_AP_SSID, AppConfig::FALLBACK_AP_PASSWORD);
    Serial.printf("[WiFi] Fallback AP started: %s\n", AppConfig::FALLBACK_AP_SSID);
    Serial.printf("[WiFi] AP password: %s\n", AppConfig::FALLBACK_AP_PASSWORD);
    logWebAddress();
}

static void connectWifi() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    if (!hasConfiguredStaCredentials()) {
        Serial.println("[WiFi] No STA credentials configured, starting fallback AP");
        startFallbackAp();
        return;
    }

    WiFi.mode(WIFI_STA);
    if (AppConfig::WIFI_DISABLE_SLEEP) {
        WiFi.setSleep(false);
    }
    WiFi.begin(AppConfig::WIFI_SSID, AppConfig::WIFI_PASSWORD);
    Serial.printf("[WiFi] Connecting to %s", AppConfig::WIFI_SSID);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < AppConfig::WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected to %s\n", AppConfig::WIFI_SSID);
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        return;
    }

    Serial.println("[WiFi] STA connection failed, switching to fallback AP");
    startFallbackAp();
}

static void maintainWifi() {
    if (!hasConfiguredStaCredentials()) {
        return;
    }

    if (WiFi.getMode() != WIFI_STA) {
        return;
    }

    uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    if (now - g_lastReconnectAttemptMs < AppConfig::WIFI_RECONNECT_INTERVAL_MS) {
        return;
    }

    g_lastReconnectAttemptMs = now;
    Serial.printf("[WiFi] Reconnecting at %s (status=%s)\n",
                  formatElapsedTime(now).c_str(),
                  wifiStatusLabel(WiFi.status()));
    WiFi.disconnect(false, false);
    WiFi.begin(AppConfig::WIFI_SSID, AppConfig::WIFI_PASSWORD);
}

static void logHeartbeat() {
    uint32_t now = millis();
    if (now - g_lastHeartbeatMs < AppConfig::HEARTBEAT_INTERVAL_MS) {
        return;
    }

    g_lastHeartbeatMs = now;
    String prefix = "[Heartbeat " + formatElapsedTime(now) + "]";
    logWifiStatus(prefix.c_str());
}

static void setupCn105Serial() {
    g_cn105Serial.begin(
        AppConfig::CN105_UART_BAUD,
        SERIAL_8E1,
        AppConfig::CN105_RX_PIN,
        AppConfig::CN105_TX_PIN
    );

    while (g_cn105Serial.available() > 0) {
        g_cn105Serial.read();
    }

    Serial.printf("[UART] CN105 serial initialized: uart=%d rx=%d tx=%d baud=%lu format=8E1\n",
                  AppConfig::CN105_UART_PORT,
                  AppConfig::CN105_RX_PIN,
                  AppConfig::CN105_TX_PIN,
                  static_cast<unsigned long>(AppConfig::CN105_UART_BAUD));
}

void setup() {
    Serial.begin(AppConfig::SERIAL_BAUD);
    delay(300);
    Serial.println();
    Serial.printf("=== %s ===\n", AppConfig::APP_TITLE);

    setupCn105Serial();

    WiFi.onEvent(handleWiFiEvent);

    connectWifi();
    g_proto.connect();

    g_webUI = new WebUI(AppConfig::WEB_PORT, &g_proto);
    g_webUI->begin();

    logWebAddress();
    Serial.println("[SETUP] Web UI ready");
}

void loop() {
    maintainWifi();
    logHeartbeat();
    g_proto.processInput();
    g_proto.loopPollCycle();
    g_webUI->loop();
    delay(2);
}
