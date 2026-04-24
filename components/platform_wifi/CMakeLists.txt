# Kiri Bridge
# CN105 HomeKit controller for Mitsubishi heat pumps
# https://kiri.dkt.moe
# https://github.com/DickyT/kiri-homekit
#
# Copyright (c) 2026
# All Rights Reserved.
# Licensed under terms of the GPL-3.0 License.

idf_component_register(SRCS "platform_wifi.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config device_settings esp_event esp_netif esp_timer esp_wifi log nvs_flash)
