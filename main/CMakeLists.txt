idf_component_register(SRCS "app_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES app_config platform_fs platform_log platform_uart platform_wifi web
                       PRIV_REQUIRES spi_flash)
