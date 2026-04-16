#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "bootstrap";

extern "C" void app_main(void) {
    esp_chip_info_t chip_info{};
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);
    esp_flash_get_size(nullptr, &flash_size);

    ESP_LOGI(TAG, "Mitsubishi Heat Pump Matter bootstrap starting");
    ESP_LOGI(TAG, "This is Phase 0: pure ESP-IDF hello world skeleton");
    ESP_LOGI(TAG,
             "Chip: cores=%d, revision=%d, features=%s%s%s%s",
             chip_info.cores,
             chip_info.revision,
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "EmbeddedFlash" : "");
    ESP_LOGI(TAG, "Flash size: %lu MB", static_cast<unsigned long>(flash_size / (1024 * 1024)));

    uint32_t heartbeat = 0;
    while (true) {
        ESP_LOGI(TAG, "Phase 0 heartbeat #%lu - waiting for next migration step",
                 static_cast<unsigned long>(heartbeat++));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
