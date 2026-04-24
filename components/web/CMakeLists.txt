# Kiri Bridge
# CN105 HomeKit controller for Mitsubishi heat pumps
# https://kiri.dkt.moe
# https://github.com/DickyT/kiri-homekit
#
# Copyright (c) 2026
# All Rights Reserved.
# Licensed under terms of the GPL-3.0 License.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(WEB_PAGES_DIR "${CMAKE_CURRENT_LIST_DIR}/pages")
set(WEB_ASSET_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_assets")
set(WEB_ASSET_SOURCES
    "${WEB_PAGES_DIR}/loader.js"
    "${WEB_PAGES_DIR}/style.css"
    "${WEB_PAGES_DIR}/tabs.html"
    "${WEB_PAGES_DIR}/root.html"
    "${WEB_PAGES_DIR}/root.js"
    "${WEB_PAGES_DIR}/logs.html"
    "${WEB_PAGES_DIR}/logs.js"
    "${WEB_PAGES_DIR}/admin.html"
    "${WEB_PAGES_DIR}/admin.js"
    "${CMAKE_CURRENT_LIST_DIR}/tools/build_assets.py")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WEB_ASSET_SOURCES})

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tools/build_assets.py"
            "--pages-dir" "${WEB_PAGES_DIR}"
            "--out-dir" "${WEB_ASSET_OUTPUT_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE WEB_ASSET_BUILD_RESULT
)

if(NOT WEB_ASSET_BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Web asset generation failed")
endif()

include("${WEB_ASSET_OUTPUT_DIR}/web_assets_manifest.cmake")

idf_component_register(SRCS "web_http.cpp"
                            "web_pages.cpp"
                            "web_routes.cpp"
                            "web_server.cpp"
                            "${WEB_ASSETS_CPP}"
                       INCLUDE_DIRS "include"
                       REQUIRES app_config app_update build_info cn105_transport core_cn105 device_settings esp_http_server esp_timer homekit_bridge jsmn platform_fs platform_log platform_maintenance platform_provisioning platform_uart platform_wifi log)

foreach(asset_file IN LISTS WEB_GZIP_ASSET_FILES)
    target_add_binary_data(${COMPONENT_LIB} "${asset_file}" BINARY)
endforeach()
