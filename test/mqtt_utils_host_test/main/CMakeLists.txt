idf_component_register(SRCS "test_main.cpp"
                            "test_cases.cpp"
                       INCLUDE_DIRS "."
                       WHOLE_ARCHIVE)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../lib/mqtt_utils ${CMAKE_CURRENT_BINARY_DIR}/mqtt_utils)

target_link_libraries(${COMPONENT_LIB} PUBLIC idf::mqtt::utils Catch2 rapidcheck)
