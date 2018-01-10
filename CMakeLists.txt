#SET(CMAKE_INCLUDE_CURRENT_DIR TRUE)
ADD_LIBRARY("mqtt" mqtt.c mqtt_msg.c ringbuf.c)

target_link_libraries(mqtt ESP8266_SDK)

