set(requires nvs_flash eventlog led esp_driver_gpio esp_driver_tsens esp_timer spi_flash lwip esp_netif dock_server usb_bridge
             net cora deck_ui settings web app_update esp_app_format matter tls)
if(IDF_TARGET STREQUAL "esp32p4")
  list(APPEND requires esp_psram)
endif()
idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES ${requires})
