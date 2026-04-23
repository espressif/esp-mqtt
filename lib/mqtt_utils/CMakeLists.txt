set(srcs mqtt_utils.c)

add_library(mqtt_utils_lib ${srcs})
target_include_directories(mqtt_utils_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(mqtt_utils_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
idf_component_get_property(log log COMPONENT_LIB)
target_link_libraries(mqtt_utils_lib PRIVATE ${log})
add_library(idf::mqtt::utils ALIAS mqtt_utils_lib)
