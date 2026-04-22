idf_component_register(SRCS "platform_wifi.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config device_settings esp_event esp_netif esp_timer esp_wifi log nvs_flash)
