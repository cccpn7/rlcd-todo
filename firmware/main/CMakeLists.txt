idf_component_register(SRCS "main.cpp" "ble.cpp" "rtc.cpp" INCLUDE_DIRS "." REQUIRES display driver esp_timer nvs_flash bt json mbedtls esp_partition esp_hw_support)
