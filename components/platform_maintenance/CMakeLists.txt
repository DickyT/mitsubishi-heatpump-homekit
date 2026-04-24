# Kiri Bridge
# CN105 HomeKit controller for Mitsubishi heat pumps
# https://kiri.dkt.moe
# https://github.com/DickyT/kiri-homekit
#
# Copyright (c) 2026
# All Rights Reserved.
# Licensed under terms of the GPL-3.0 License.

idf_component_register(SRCS "platform_maintenance.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_hap_core esp_system nvs_flash platform_fs platform_log)
