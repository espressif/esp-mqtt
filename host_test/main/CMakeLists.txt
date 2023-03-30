idf_component_register(SRCS  "test_mqtt_client.cpp"
                       INCLUDE_DIRS "$ENV{IDF_PATH}/tools/catch"
                       REQUIRES cmock mqtt esp_timer esp_hw_support http_parser log)

target_compile_options(${COMPONENT_LIB} PUBLIC -fsanitize=address -fconcepts)
target_link_options(${COMPONENT_LIB} PUBLIC -fsanitize=address)

idf_component_get_property(mqtt mqtt COMPONENT_LIB)
target_compile_options(${mqtt} PUBLIC -fsanitize=address -fconcepts)
target_link_options(${mqtt} PUBLIC -fsanitize=address)

if(CONFIG_GCOV_ENABLED)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC  --coverage -fprofile-arcs -ftest-coverage)
    
    idf_component_get_property(mqtt mqtt COMPONENT_LIB)
    target_compile_options(${mqtt} PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(${mqtt} PUBLIC  --coverage -fprofile-arcs -ftest-coverage)
endif()
