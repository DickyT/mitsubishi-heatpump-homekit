#include "platform_led.h"

#include "app_config.h"
#include "cn105_transport.h"
#include "device_settings.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "platform_wifi.h"

namespace {

const char* TAG = "platform_led";

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct State {
    led_strip_handle_t strip = nullptr;
    bool taskRunning = false;
    Color applied = {};
};

State state;

constexpr Color kOff = {0, 0, 0};
constexpr Color kGreen = {0, 255, 0};
constexpr Color kBlue = {0, 0, 255};
constexpr Color kOrange = {255, 40, 0};
constexpr Color kRed = {255, 0, 0};

Color scaled(Color color) {
    const uint32_t brightness = app_config::kStatusLedBrightness;
    color.r = static_cast<uint8_t>((static_cast<uint32_t>(color.r) * brightness) / 255);
    color.g = static_cast<uint8_t>((static_cast<uint32_t>(color.g) * brightness) / 255);
    color.b = static_cast<uint8_t>((static_cast<uint32_t>(color.b) * brightness) / 255);
    return color;
}

bool sameColor(const Color& left, const Color& right) {
    return left.r == right.r && left.g == right.g && left.b == right.b;
}

void applyColor(Color color) {
    if (state.strip == nullptr) {
        return;
    }

    color = scaled(color);
    if (sameColor(color, state.applied)) {
        return;
    }

    if (color.r == 0 && color.g == 0 && color.b == 0) {
        led_strip_clear(state.strip);
    } else {
        led_strip_set_pixel(state.strip, 0, color.r, color.g, color.b);
        led_strip_refresh(state.strip);
    }
    state.applied = color;
}

bool isCn105Healthy(const cn105_transport::Status& transport) {
    return !device_settings::useRealCn105() || transport.connected;
}

Color selectColor() {
    if (!device_settings::statusLedEnabled()) {
        return kOff;
    }

    const platform_wifi::Status wifi = platform_wifi::getStatus();
    const cn105_transport::Status transport = cn105_transport::getStatus();
    const bool wifiHealthy = wifi.staConnected;
    const bool cn105Healthy = isCn105Healthy(transport);

    if (!wifiHealthy && !cn105Healthy) {
        return kRed;
    }
    if (!wifiHealthy) {
        return kBlue;
    }
    if (!cn105Healthy) {
        return kOrange;
    }

    return kGreen;
}

void ledTask(void*) {
    state.taskRunning = true;
    ESP_LOGI(TAG, "Status LED task started on GPIO%d", app_config::kStatusLedPin);

    while (true) {
        applyColor(selectColor());
        vTaskDelay(pdMS_TO_TICKS(app_config::kStatusLedUpdateIntervalMs));
    }
}

}  // namespace

namespace platform_led {

esp_err_t init() {
    if (!app_config::kStatusLedEnabled) {
        ESP_LOGI(TAG, "Status LED disabled by config");
        return ESP_OK;
    }
    if (state.taskRunning) {
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = app_config::kStatusLedPin,
        .max_leds = static_cast<uint32_t>(app_config::kStatusLedPixels),
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags = {
            .with_dma = false,
        },
    };

    const esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &state.strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        return err;
    }

    led_strip_clear(state.strip);
    const BaseType_t created = xTaskCreate(ledTask,
                                           "status_led",
                                           app_config::kStatusLedTaskStackBytes,
                                           nullptr,
                                           1,
                                           nullptr);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create status LED task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

}  // namespace platform_led
