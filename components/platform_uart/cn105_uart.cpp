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

#include "cn105_uart.h"

#include "app_config.h"
#include "device_settings.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

namespace {

const char* TAG = "cn105_uart";
bool initialized = false;

uart_word_length_t dataBitsFromSetting(int data_bits) {
    switch (data_bits) {
        case 8:
        default:
            return UART_DATA_8_BITS;
    }
}

uart_parity_t parityFromSetting(char parity) {
    switch (parity) {
        case 'N':
            return UART_PARITY_DISABLE;
        case 'O':
            return UART_PARITY_ODD;
        case 'E':
        default:
            return UART_PARITY_EVEN;
    }
}

uart_stop_bits_t stopBitsFromSetting(int stop_bits) {
    switch (stop_bits) {
        case 2:
            return UART_STOP_BITS_2;
        case 1:
        default:
            return UART_STOP_BITS_1;
    }
}

}  // namespace

namespace cn105_uart {

esp_err_t init() {
    const gpio_num_t rx_pin = static_cast<gpio_num_t>(device_settings::cn105RxPin());
    const gpio_num_t tx_pin = static_cast<gpio_num_t>(device_settings::cn105TxPin());
    const uart_config_t config = {
        .baud_rate = device_settings::cn105BaudRate(),
        .data_bits = dataBitsFromSetting(device_settings::cn105DataBits()),
        .parity = parityFromSetting(device_settings::cn105Parity()),
        .stop_bits = stopBitsFromSetting(device_settings::cn105StopBits()),
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };

    ESP_LOGI(TAG,
             "Initializing CN105 UART: uart=%d rx=%d tx=%d baud=%d format=%s rxPull=%s txOD=%s",
             app_config::kCn105UartPort,
             static_cast<int>(rx_pin),
             static_cast<int>(tx_pin),
             device_settings::cn105BaudRate(),
             device_settings::cn105FormatName(),
             device_settings::cn105RxPullupEnabled() ? "on" : "off",
             device_settings::cn105TxOpenDrain() ? "on" : "off");

    esp_err_t err = uart_param_config(app_config::kCn105UartPort, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(app_config::kCn105UartPort,
                       tx_pin,
                       rx_pin,
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

    if (device_settings::cn105RxPullupEnabled()) {
        err = gpio_pullup_en(rx_pin);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_pullup_en(rx) failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "CN105 RX pullup enabled on GPIO%d", static_cast<int>(rx_pin));
    }

    initialized = true;
    ESP_LOGI(TAG, "CN105 UART ready");
    return ESP_OK;
}

Status getStatus() {
    return Status{
        .initialized = initialized,
        .uart = static_cast<int>(app_config::kCn105UartPort),
        .rxPin = device_settings::cn105RxPin(),
        .txPin = device_settings::cn105TxPin(),
        .baudRate = device_settings::cn105BaudRate(),
        .format = device_settings::cn105FormatName(),
    };
}

}  // namespace cn105_uart
