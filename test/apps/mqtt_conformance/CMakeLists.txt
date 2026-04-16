# The following boilerplate must appear in this order.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS "../common")
idf_build_set_property(MINIMAL_BUILD ON)

project(mqtt_conformance)
