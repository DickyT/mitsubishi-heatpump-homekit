#include <stdio.h>

#include "app_config.h"
#include "cn105_uart.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_fs.h"
#include "platform_log.h"

static const char* TAG = "bootstrap";

extern "C" void app_main(void) {
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

    ESP_LOGI(TAG, "Mitsubishi Heat Pump Matter bootstrap starting");
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

    const esp_err_t uart_err = cn105_uart::init();
    if (uart_err != ESP_OK) {
        ESP_LOGE(TAG, "CN105 UART init failed: %s", esp_err_to_name(uart_err));
    }

    uint32_t heartbeat = 0;
    while (true) {
        ESP_LOGI(TAG, "Phase 2 heartbeat #%lu - filesystem logging is alive",
                 static_cast<unsigned long>(heartbeat++));
        vTaskDelay(pdMS_TO_TICKS(app_config::kHeartbeatIntervalMs));
    }
}
