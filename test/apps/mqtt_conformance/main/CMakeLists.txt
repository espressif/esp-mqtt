idf_component_register(SRCS "mqtt_conformance.c" "mqtt_conformance_console.c"
                       INCLUDE_DIRS "."
                       REQUIRES mqtt nvs_flash console esp_netif)
