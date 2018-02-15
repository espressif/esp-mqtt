deps_config := \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/app_trace/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/aws_iot/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/bt/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/esp32/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/ethernet/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/fatfs/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/freertos/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/heap/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/libsodium/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/log/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/lwip/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/mbedtls/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/openssl/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/pthread/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/spi_flash/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/spiffs/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/tcpip_adapter/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/wear_levelling/Kconfig \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/bootloader/Kconfig.projbuild \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/esptool_py/Kconfig.projbuild \
	/Users/TuanPM/Projects/2018/tuanpm/esp32-mqtt/components/espmqtt/examples/mqtt_tcp/main/Kconfig.projbuild \
	/Volumes/tools/esp32/sdk/esp-idf-github/components/partition_table/Kconfig.projbuild \
	/Volumes/tools/esp32/sdk/esp-idf-github/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
