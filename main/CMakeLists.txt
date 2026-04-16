idf_component_register(SRCS "app_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES app_config platform_log platform_uart
                       PRIV_REQUIRES spi_flash)
