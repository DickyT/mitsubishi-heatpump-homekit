idf_component_register(SRCS "platform_maintenance.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_hap_core esp_system nvs_flash platform_fs platform_log)
