idf_component_register(SRCS  "test_mqtt_client.cpp" "test_log_intercept.cpp" "test_log_matchers.cpp" "test_log_parser.cpp"
                       REQUIRES cmock mqtt esp_timer esp_hw_support http_parser log
                       WHOLE_ARCHIVE)

set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined)

target_compile_options(${COMPONENT_LIB} PUBLIC ${SANITIZER_FLAGS} -Wno-missing-field-initializers)
target_link_options(${COMPONENT_LIB} PUBLIC ${SANITIZER_FLAGS})
target_link_libraries(${COMPONENT_LIB} PUBLIC Catch2::Catch2WithMain rapidcheck rapidcheck_catch)

idf_component_get_property(mqtt mqtt COMPONENT_LIB)
target_compile_definitions(${mqtt} PRIVATE SOC_WIFI_SUPPORTED=1)
target_compile_options(${mqtt} PUBLIC ${SANITIZER_FLAGS})
target_link_options(${mqtt} PUBLIC ${SANITIZER_FLAGS})

if(CONFIG_GCOV_ENABLED)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC  --coverage -fprofile-arcs -ftest-coverage)

    idf_component_get_property(mqtt mqtt COMPONENT_LIB)
    target_compile_options(${mqtt} PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(${mqtt} PUBLIC  --coverage -fprofile-arcs -ftest-coverage)
endif()
