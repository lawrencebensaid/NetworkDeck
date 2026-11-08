idf_component_register(SRCS "dock_server.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES cora usb_bridge
                       PRIV_REQUIRES lwip esp_timer)
