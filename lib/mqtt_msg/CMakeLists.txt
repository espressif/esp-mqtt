set(srcs "${CMAKE_CURRENT_LIST_DIR}/mqtt_msg.c")

if(CONFIG_MQTT_PROTOCOL_5)
    list(APPEND srcs "${CMAKE_CURRENT_LIST_DIR}/mqtt5_msg.c")
endif()

add_library(mqtt_msg_lib ${srcs})
target_include_directories(mqtt_msg_lib
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/../../include
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include)

idf_component_get_property(heap heap COMPONENT_LIB)
idf_component_get_property(log log COMPONENT_LIB)
target_link_libraries(mqtt_msg_lib PRIVATE ${heap} ${log})

add_library(idf::mqtt::msg ALIAS mqtt_msg_lib)
