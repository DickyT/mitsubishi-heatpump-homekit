idf_component_register(SRCS "cn105_uart.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config device_settings esp_driver_gpio esp_driver_uart log)
