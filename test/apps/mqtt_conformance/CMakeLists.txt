# The following boilerplate must appear in this order.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS "../common")
idf_build_set_property(MINIMAL_BUILD ON)

project(mqtt_conformance)

if(CONFIG_ESP_GCOV_ENABLE)
    get_filename_component(mqtt_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    idf_component_get_property(mqtt mqtt COMPONENT_LIB)
    target_compile_options(${mqtt} PRIVATE --coverage)
    if(DEFINED ENV{CI})
        get_filename_component(ci_artifact_root "${mqtt_root}/.." ABSOLUTE)
        target_compile_options(
            ${mqtt}
            PRIVATE
            "-fprofile-dir=."
            "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
            "-fprofile-prefix-map=${mqtt_root}=${ci_artifact_root}")
    endif()
    target_link_options(${mqtt} PUBLIC --coverage)
endif()

if(CONFIG_ESP_GCOV_ENABLE AND COMMAND idf_create_coverage_report)
    idf_create_coverage_report(
        ${CMAKE_CURRENT_BINARY_DIR}/coverage_report
        SOURCE_DIR ${mqtt_root}
        GCOV_OPTIONS
            "--filter=${mqtt_root}/mqtt_client.c"
            "--filter=${mqtt_root}/mqtt5_client.c"
            "--filter=${mqtt_root}/lib/mqtt_msg.c"
            "--filter=${mqtt_root}/lib/mqtt5_msg.c"
            "--filter=${mqtt_root}/lib/platform.c"
            "--xml=${CMAKE_CURRENT_BINARY_DIR}/coverage_report/coverage.xml")
    idf_clean_coverage_report(${CMAKE_CURRENT_BINARY_DIR}/coverage_report)
endif()
