cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/build_version.cmake OPTIONAL)

set(HOMEKIT_PATH ${CMAKE_CURRENT_LIST_DIR}/external/esp-homekit-sdk)
set(EXTRA_COMPONENT_DIRS
    ${HOMEKIT_PATH}/components
    ${HOMEKIT_PATH}/components/homekit)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mitsubishi_heatpump_homekit)
