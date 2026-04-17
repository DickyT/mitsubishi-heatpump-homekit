idf_component_register(SRCS "homekit_bridge.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config cn105_transport core_cn105 esp_event esp_hap_apple_profiles esp_hap_core esp_timer log)
