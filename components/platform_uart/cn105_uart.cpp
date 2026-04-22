#include "cn105_uart.h"

#include "app_config.h"
#include "device_settings.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

namespace {

const char* TAG = "cn105_uart";
bool initialized = false;

const char* parityName(uart_parity_t parity) {
    switch (parity) {
        case UART_PARITY_DISABLE:
            return "N";
        case UART_PARITY_EVEN:
            return "E";
        case UART_PARITY_ODD:
            return "O";
        default:
            return "?";
    }
}

}  // namespace

namespace cn105_uart {

esp_err_t init() {
    const uart_config_t config = {
        .baud_rate = app_config::kCn105BaudRate,
        .data_bits = app_config::kCn105DataBits,
        .parity = app_config::kCn105Parity,
        .stop_bits = app_config::kCn105StopBits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };

    ESP_LOGI(TAG,
             "Initializing CN105 UART: uart=%d rx=%d tx=%d baud=%d format=8%s1",
             app_config::kCn105UartPort,
             app_config::kCn105RxPin,
             app_config::kCn105TxPin,
             device_settings::cn105BaudRate(),
             parityName(app_config::kCn105Parity));

    uart_config_t runtime_config = config;
    runtime_config.baud_rate = device_settings::cn105BaudRate();

    esp_err_t err = uart_param_config(app_config::kCn105UartPort, &runtime_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(app_config::kCn105UartPort,
                       app_config::kCn105TxPin,
                       app_config::kCn105RxPin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(app_config::kCn105UartPort,
                              app_config::kCn105RxBufferBytes,
                              app_config::kCn105TxBufferBytes,
                              0,
                              nullptr,
                              0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    if (app_config::kCn105RxPullupEnabled) {
        err = gpio_pullup_en(app_config::kCn105RxPin);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_pullup_en(rx) failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "CN105 RX pullup enabled on GPIO%d", static_cast<int>(app_config::kCn105RxPin));
    }

    initialized = true;
    ESP_LOGI(TAG, "CN105 UART ready");
    return ESP_OK;
}

Status getStatus() {
    return Status{
        .initialized = initialized,
        .uart = static_cast<int>(app_config::kCn105UartPort),
        .rxPin = static_cast<int>(app_config::kCn105RxPin),
        .txPin = static_cast<int>(app_config::kCn105TxPin),
        .baudRate = device_settings::cn105BaudRate(),
        .format = "8E1",
    };
}

}  // namespace cn105_uart
