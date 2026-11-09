idf_component_register(SRCS "net.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES settings
                       PRIV_REQUIRES esp_eth esp_netif esp_event esp_hw_support esp_timer esp_wifi lwip eventlog
                                     espressif__mdns espressif__esp_wifi_remote)
