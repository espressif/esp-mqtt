idf_component_register(SRCS "test_main.cpp" "test_outbox.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES freertos
                       WHOLE_ARCHIVE)

set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined)
target_compile_options(${COMPONENT_LIB} PRIVATE ${SANITIZER_FLAGS} -Wno-missing-field-initializers)
target_link_options(${COMPONENT_LIB} INTERFACE ${SANITIZER_FLAGS})

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../lib/mqtt_outbox
                 ${CMAKE_CURRENT_BINARY_DIR}/mqtt_outbox)

target_link_libraries(${COMPONENT_LIB} PUBLIC
    idf::mqtt::outbox
    Catch2::Catch2WithMain
    rapidcheck
    rapidcheck_catch)

if(CONFIG_GCOV_ENABLED)
    target_compile_options(mqtt_outbox_lib PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(mqtt_outbox_lib PUBLIC --coverage -fprofile-arcs -ftest-coverage)
endif()
