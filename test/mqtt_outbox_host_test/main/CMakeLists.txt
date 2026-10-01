idf_component_register(SRCS "test_main.cpp" "test_outbox.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES freertos
                       WHOLE_ARCHIVE)

set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined)
target_compile_options(${COMPONENT_LIB} PRIVATE ${SANITIZER_FLAGS} -Wno-missing-field-initializers)
target_link_options(${COMPONENT_LIB} INTERFACE ${SANITIZER_FLAGS})

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../lib/mqtt_outbox
                 ${CMAKE_CURRENT_BINARY_DIR}/mqtt_outbox)

# mqtt_outbox.c now owns mqtt_message_t and calls mqtt_msg_destroy().
add_library(mqtt_msg_lib "${CMAKE_CURRENT_LIST_DIR}/../../../lib/mqtt_msg.c")
target_include_directories(mqtt_msg_lib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../../lib/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../include)
idf_component_get_property(heap heap COMPONENT_LIB)
idf_component_get_property(log log COMPONENT_LIB)
target_link_libraries(mqtt_msg_lib PRIVATE ${heap} ${log})
target_compile_options(mqtt_msg_lib PRIVATE ${SANITIZER_FLAGS})

target_link_libraries(${COMPONENT_LIB} PUBLIC
    idf::mqtt::outbox
    mqtt_msg_lib
    Catch2::Catch2WithMain
    rapidcheck
    rapidcheck_catch)

if(CONFIG_GCOV_ENABLED)
    target_compile_options(mqtt_outbox_lib PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(mqtt_outbox_lib PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_compile_options(mqtt_msg_lib PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(mqtt_msg_lib PUBLIC --coverage -fprofile-arcs -ftest-coverage)
endif()
