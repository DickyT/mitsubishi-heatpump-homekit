cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/build_version.cmake OPTIONAL)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__led_strip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mitsubishi_installer_probe)
