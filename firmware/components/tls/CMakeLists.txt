# The dock's own certificate authority and HTTPS identity.
idf_component_register(SRCS "tls_cert.c" "tls_names.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       PRIV_REQUIRES mbedtls nvs_flash esp_hw_support log freertos)
