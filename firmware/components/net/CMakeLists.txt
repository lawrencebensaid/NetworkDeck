idf_component_register(SRCS "net.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_eth esp_netif esp_event esp_hw_support espressif__mdns)
