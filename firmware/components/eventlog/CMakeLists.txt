idf_component_register(SRCS "eventlog.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_partition spi_flash esp_timer)
