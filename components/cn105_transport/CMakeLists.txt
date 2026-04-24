# Kiri Bridge
# CN105 HomeKit controller for Mitsubishi heat pumps
# https://kiri.dkt.moe
# https://github.com/DickyT/kiri-homekit
#
# Copyright (c) 2026
# All Rights Reserved.
# Licensed under terms of the GPL-3.0 License.

idf_component_register(SRCS "cn105_transport.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config core_cn105 device_settings esp_driver_uart esp_timer freertos log)
