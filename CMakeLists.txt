set(srcs mqtt_client.c lib/platform.c)

if(CONFIG_MQTT_PROTOCOL_5)
    list(APPEND srcs mqtt5_client.c)
endif()

set(priv_requires esp_timer http_parser esp_hw_support heap)
idf_build_get_property(target IDF_TARGET)
if(NOT target STREQUAL "linux")
    # eventfd, to wake the client task when a message is enqueued
    list(APPEND priv_requires vfs)
endif()

list(TRANSFORM srcs PREPEND ${CMAKE_CURRENT_LIST_DIR}/)
idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include ${CMAKE_CURRENT_LIST_DIR}/lib/mqtt_msg/include
                    PRIV_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/lib/include
                    REQUIRES esp_event tcp_transport
                    PRIV_REQUIRES ${priv_requires}
                    KCONFIG ${CMAKE_CURRENT_LIST_DIR}/Kconfig
                    )

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/mqtt_utils)
target_link_libraries(${COMPONENT_LIB} PUBLIC idf::mqtt::utils)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/mqtt_msg)
target_link_libraries(${COMPONENT_LIB} PRIVATE idf::mqtt::msg)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/mqtt_outbox)
target_link_libraries(${COMPONENT_LIB} PRIVATE idf::mqtt::outbox)
