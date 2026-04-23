#include <stdio.h>

#include "app_config.h"
#include "build_info.h"
#include "cn105_core.h"
#include "cn105_transport.h"
#include "cn105_uart.h"
#include "device_settings.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "homekit_bridge.h"
#include "platform_fs.h"
#include "platform_led.h"
#include "platform_log.h"
#include "platform_provisioning.h"
#include "platform_wifi.h"
#include "web_server.h"

static const char* TAG = "bootstrap";

extern "C" void app_main(void) {
    const esp_err_t settings_err = device_settings::init();
    if (settings_err != ESP_OK) {
        ESP_LOGE(TAG, "Device settings init failed: %s", esp_err_to_name(settings_err));
    }
    platform_log::init();

    const esp_err_t fs_err = platform_fs::init();
    if (fs_err == ESP_OK) {
        platform_log::enablePersistentLog();
    } else {
        ESP_LOGE(TAG, "SPIFFS init failed: %s", esp_err_to_name(fs_err));
    }

    esp_chip_info_t chip_info{};
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);
    esp_flash_get_size(nullptr, &flash_size);

    ESP_LOGI(TAG, "Mitsubishi Heat Pump HomeKit bootstrap starting");
    ESP_LOGI(TAG, "Firmware version: %s", build_info::firmwareVersion());
    platform_log::logStartupSummary();
    ESP_LOGI(TAG,
             "Chip: cores=%d, revision=%d, features=%s%s%s%s",
             chip_info.cores,
             chip_info.revision,
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "EmbeddedFlash" : "");
    ESP_LOGI(TAG, "Flash size: %lu MB", static_cast<unsigned long>(flash_size / (1024 * 1024)));

    const esp_err_t led_err = platform_led::init();
    if (led_err != ESP_OK) {
        ESP_LOGE(TAG, "Status LED init failed: %s", esp_err_to_name(led_err));
    }

    const esp_err_t uart_err = cn105_uart::init();
    if (uart_err != ESP_OK) {
        ESP_LOGE(TAG, "CN105 UART init failed: %s", esp_err_to_name(uart_err));
    }

    cn105_core::initMockState();
    char cn105_self_test_error[96] = {};
    if (!cn105_core::runSelfTest(cn105_self_test_error, sizeof(cn105_self_test_error))) {
        ESP_LOGW(TAG, "CN105 offline self-test failed: %s", cn105_self_test_error);
    }

    if (device_settings::useRealCn105()) {
        const esp_err_t transport_err = cn105_transport::start();
        if (transport_err != ESP_OK) {
            ESP_LOGE(TAG, "CN105 transport start failed: %s", esp_err_to_name(transport_err));
        } else {
            ESP_LOGI(TAG, "CN105 real transport started");
        }
    } else {
        ESP_LOGI(TAG, "CN105 transport: mock mode");
    }

    const esp_err_t wifi_err = platform_wifi::init();
    if (wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(wifi_err));
    }

    const esp_err_t provisioning_err = platform_provisioning::init();
    if (provisioning_err != ESP_OK) {
        ESP_LOGE(TAG, "BLE provisioning init failed: %s", esp_err_to_name(provisioning_err));
    }

    const esp_err_t homekit_err = homekit_bridge::start();
    if (homekit_err != ESP_OK) {
        ESP_LOGE(TAG, "HomeKit start failed: %s", esp_err_to_name(homekit_err));
    }

    const esp_err_t web_err = web_server::start();
    if (web_err != ESP_OK) {
        ESP_LOGE(TAG, "WebUI start failed: %s", esp_err_to_name(web_err));
    }

    uint32_t heartbeat = 0;
    while (true) {
        platform_wifi::maintain();
        if (cn105_core::isMockDirty()) {
            homekit_bridge::syncFromMock();
            cn105_core::clearMockDirty();
        }
        ESP_LOGI(TAG, "Platform heartbeat #%lu - services are alive",
                 static_cast<unsigned long>(heartbeat));
        platform_wifi::logStatus("heartbeat");
        heartbeat++;
        vTaskDelay(pdMS_TO_TICKS(app_config::kHeartbeatIntervalMs));
    }
}
