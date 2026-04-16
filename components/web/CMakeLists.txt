idf_component_register(SRCS "web_server.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config esp_http_server esp_timer platform_fs platform_uart platform_wifi log)
