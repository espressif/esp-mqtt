idf_component_register(SRCS "mqtt_conformance.cpp" "mqtt_conformance_console.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES mqtt nvs_flash console esp_netif nlohmann-json)

set_source_files_properties(mqtt_conformance.cpp PROPERTIES
                            COMPILE_OPTIONS "-Wno-deprecated-declarations")

target_compile_definitions(${COMPONENT_LIB} PRIVATE JSON_NO_IO)
