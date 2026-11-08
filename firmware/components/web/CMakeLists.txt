idf_component_register(SRCS "web.cpp"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "index.html" "login.html"
                       REQUIRES settings
                       PRIV_REQUIRES esp_http_server app_update lwip esp_system esp_timer nvs_flash mbedtls)
