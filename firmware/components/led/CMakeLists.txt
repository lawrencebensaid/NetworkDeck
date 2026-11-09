idf_component_register(SRCS "led.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES espressif__led_strip esp_driver_gpio)
