# Kiri Bridge
# CN105 HomeKit controller for Mitsubishi heat pumps
# https://kiri.dkt.moe
# https://github.com/DickyT/kiri-homekit
#
# Copyright (c) 2026
# All Rights Reserved.
# Licensed under terms of the GPL-3.0 License.

idf_component_register(SRCS "platform_led.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config cn105_transport device_settings esp_timer led_strip log platform_provisioning platform_wifi)
