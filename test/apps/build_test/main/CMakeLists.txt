idf_component_register(SRCS "mqtt_app.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES nvs_flash esp_netif)
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
