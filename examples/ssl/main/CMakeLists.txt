idf_build_get_property(idf_target IDF_TARGET)
set(ssl_priv_requires mqtt esp_partition nvs_flash esp_netif)
if(NOT idf_target STREQUAL "linux")
    list(APPEND ssl_priv_requires app_update)
endif()
idf_component_register(SRCS "app_main.c"
                    PRIV_REQUIRES ${ssl_priv_requires}
                    INCLUDE_DIRS ".")
