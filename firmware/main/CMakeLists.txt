idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nvs_flash dock_server usb_bridge net cora)
