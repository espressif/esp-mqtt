cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/CPM.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/RapidCheck.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)

project(mqtt_utils_host_test)
