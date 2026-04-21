idf_component_register(SRCS "platform_led.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config cn105_transport esp_timer led_strip log platform_wifi)
