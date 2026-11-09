# Matter only on the ESP32-P4 for now (it needs PSRAM next to the web interface and HTTPS).
if(IDF_TARGET STREQUAL "esp32p4")
  idf_component_register(SRCS "matter.cpp"
                         INCLUDE_DIRS "include"
                         PRIV_REQUIRES nvs_flash esp_timer eventlog)
else()
  idf_component_register(SRCS "matter_none.cpp"
                         INCLUDE_DIRS "include")
endif()
