idf_component_register(SRCS "device_settings.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config esp_driver_uart log nvs_flash)
