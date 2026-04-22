idf_component_register(SRCS "app_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES app_update esp_driver_gpio esp_driver_uart esp_event esp_http_server esp_netif esp_wifi led_strip nvs_flash wifi_provisioning)
