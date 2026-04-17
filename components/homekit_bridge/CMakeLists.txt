idf_component_register(SRCS "homekit_bridge.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config core_cn105 esp_event esp_hap_apple_profiles esp_hap_core log)
