set(srcs "${CMAKE_CURRENT_LIST_DIR}/../mqtt_outbox.c")

add_library(mqtt_outbox_lib ${srcs})
target_include_directories(mqtt_outbox_lib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
idf_component_get_property(heap heap COMPONENT_LIB)
idf_component_get_property(log  log  COMPONENT_LIB)
target_link_libraries(mqtt_outbox_lib PRIVATE ${heap} ${log})
add_library(idf::mqtt::outbox ALIAS mqtt_outbox_lib)
