idf_component_register(SRCS "platform_provisioning.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config device_settings esp_driver_gpio esp_event esp_timer esp_wifi log platform_lock wifi_provisioning)
