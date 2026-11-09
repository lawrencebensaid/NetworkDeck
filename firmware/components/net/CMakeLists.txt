set(requires esp_eth esp_netif esp_event esp_hw_support esp_timer esp_wifi lwip eventlog espressif__mdns)
if(IDF_TARGET STREQUAL "esp32p4")
  list(APPEND requires espressif__esp_wifi_remote)
endif()
idf_component_register(SRCS "net.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES settings
                       PRIV_REQUIRES ${requires})
