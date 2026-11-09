idf_component_register(SRCS "web.cpp"
                       INCLUDE_DIRS "include"
                       EMBED_FILES "assets/index.html.gz"
                       REQUIRES settings
                       PRIV_REQUIRES auth eventlog tls esp_http_server esp_https_server app_update lwip esp_system)
