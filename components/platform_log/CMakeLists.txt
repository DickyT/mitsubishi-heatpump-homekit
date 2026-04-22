idf_component_register(SRCS "platform_log.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config device_settings esp_timer freertos log platform_fs platform_lock)
