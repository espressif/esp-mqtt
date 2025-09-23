# The following four lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
list(APPEND EXTRA_COMPONENT_DIRS
  "$ENV{IDF_PATH}/tools/mocks/esp_hw_support/"
  "$ENV{IDF_PATH}/tools/mocks/freertos/"
  "$ENV{IDF_PATH}/tools/mocks/esp_timer/"
  "$ENV{IDF_PATH}/tools/mocks/esp_event/"
  "$ENV{IDF_PATH}/tools/mocks/lwip/"
  "$ENV{IDF_PATH}/tools/mocks/esp-tls/"
  "$ENV{IDF_PATH}/tools/mocks/http_parser/"
  "$ENV{IDF_PATH}/tools/mocks/tcp_transport/")

project(host_mqtt_client_test)
