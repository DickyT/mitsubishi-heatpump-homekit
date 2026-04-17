idf_component_register(SRCS "cn105_transport.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config core_cn105 esp_driver_uart esp_timer freertos log)
