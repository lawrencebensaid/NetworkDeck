idf_component_register(SRCS "usb_bridge.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES hidparse
                       PRIV_REQUIRES espressif__usb)
