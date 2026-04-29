idf_component_register(
    SRCS         "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES     improv_wifi_busware
                 nvs_flash
                 esp_wifi
                 esp_event
                 esp_netif
                 esp_timer
                 esp_http_server
                 driver
                 log
)
