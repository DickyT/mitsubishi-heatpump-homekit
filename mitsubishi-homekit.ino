#include "src/AppConfig.h"
#include "src/DebugLog.h"
#include "src/Cn105Serial.h"
#include "src/HomeKitManager.h"
#include "src/HomeKitReset.h"
#include "src/MitsubishiProtocol.h"
#include "src/WebUI.h"
#include "src/WiFiManager.h"

HardwareSerial g_cn105Serial(AppConfig::CN105_UART_PORT);
MitsubishiProtocol g_proto(&g_cn105Serial);
WiFiManager g_wifiManager;
WebUI* g_webUI = nullptr;

static void setupWebUI() {
    g_webUI = new WebUI(
        AppConfig::WEB_PORT,
        &g_proto,
        AppConfig::HOMEKIT_PAIRING_CODE,
        AppConfig::HOMEKIT_DEVICE_NAME,
        HomeKitReset::clearHomeKitData,
        HomeKitReset::clearAllHomeSpanData,
        HomeKitReset::rebootNow
    );
    g_webUI->begin();

    g_wifiManager.logWebAddress();
    DebugLog::printf("[SETUP] HomeKit device: %s\n", AppConfig::HOMEKIT_DEVICE_NAME);
    DebugLog::printf("[SETUP] HomeKit pairing code: %s\n", AppConfig::HOMEKIT_PAIRING_CODE);
    DebugLog::printf("[SETUP] HomeKit QR ID: %s\n", AppConfig::HOMEKIT_QR_ID);
    DebugLog::printf("[SETUP] Web UI: http://%s:%u/\n",
                  g_wifiManager.webHostIp().c_str(),
                  AppConfig::WEB_PORT);
}

void setup() {
    DebugLog::begin(AppConfig::SERIAL_BAUD);
    delay(300);
    DebugLog::println();
    DebugLog::printf("=== %s ===\n", AppConfig::APP_TITLE);
    DebugLog::printf("[SETUP] CN105 transport mode: %s\n",
                  AppConfig::cn105TransportModeLabel(AppConfig::CN105_TRANSPORT_MODE));

    Cn105Serial::begin(g_cn105Serial);
    g_wifiManager.begin();
    DebugLog::beginPersistentLog();
    g_proto.connect();
    HomeKitManager::begin(&g_proto);
    setupWebUI();

    DebugLog::println("[SETUP] Web UI ready");
}

void loop() {
    g_wifiManager.maintain();
    g_wifiManager.logHeartbeat();
    g_proto.processInput();
    g_proto.loopPollCycle();
    HomeKitManager::poll();
    g_webUI->loop();
    delay(2);
}
