idf_component_register(SRCS "web.cpp"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "index.html"
                       REQUIRES settings
                       PRIV_REQUIRES esp_http_server app_update lwip esp_system)
