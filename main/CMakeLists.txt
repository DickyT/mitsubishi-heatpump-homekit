idf_component_register(SRCS "app_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES app_config core_cn105 cn105_transport homekit_bridge platform_fs platform_log platform_uart platform_wifi web
                       PRIV_REQUIRES nvs_flash spi_flash)
