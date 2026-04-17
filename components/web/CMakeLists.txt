idf_component_register(SRCS "web_http.cpp"
                            "web_pages.cpp"
                            "web_routes.cpp"
                            "web_server.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config core_cn105 esp_http_server esp_timer homekit_bridge platform_fs platform_uart platform_wifi log)
