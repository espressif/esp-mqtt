cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/CPM.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/RapidCheck.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)

project(mqtt_msg_host_test)
