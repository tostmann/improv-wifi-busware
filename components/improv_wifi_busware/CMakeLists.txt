# ESP-IDF component manifest.
#
# NOTE: the current sources still depend on the Arduino Core for ESP32
# (Arduino.h, WiFi.h, Stream). Building this component under pure ESP-IDF
# requires either the arduino-esp32 component being present in the same
# project, or the ongoing port to native ESP-IDF APIs (esp_wifi, usb_serial_jtag).
# Both paths are tracked in the repo README.

idf_component_register(
    SRCS         "src/ImprovWiFiLibrary.cpp"
    INCLUDE_DIRS "include"
    REQUIRES     arduino
)
