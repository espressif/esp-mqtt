set(srcs mqtt_client.c lib/mqtt_msg.c lib/mqtt_outbox.c lib/platform_esp32_idf.c)

if(CONFIG_MQTT_PROTOCOL_5)
    list(APPEND srcs lib/mqtt5_msg.c mqtt5_client.c)
endif()

list(TRANSFORM srcs PREPEND ${CMAKE_CURRENT_LIST_DIR}/)
idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include
                    PRIV_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/lib/include
                    REQUIRES esp_event tcp_transport
                    PRIV_REQUIRES esp_timer http_parser esp_hw_support heap
                    KCONFIG ${CMAKE_CURRENT_LIST_DIR}/Kconfig
                    )
