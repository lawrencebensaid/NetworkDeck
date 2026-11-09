idf_component_register(SRCS "auth.cpp" "passkeys.cpp" "webauthn.cpp" "apikeys.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash mbedtls esp_timer esp_hw_support)
