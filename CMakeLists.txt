cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${IDF_TARGET}")
idf_build_set_property(COMPILE_OPTIONS "-DM5_LOG_LEVEL=3" "-DCORE_DEBUG_LEVEL=3" APPEND)

project(espnow-walkie-talkie)
