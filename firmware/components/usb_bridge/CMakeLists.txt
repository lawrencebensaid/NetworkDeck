# The USB host bridge needs a USB OTG controller (ESP32-P4, -S3, -S2); other chips get a stub.
set(otg_targets esp32p4 esp32s3 esp32s2)
if(IDF_TARGET IN_LIST otg_targets)
  idf_component_register(SRCS "usb_bridge.cpp"
                         INCLUDE_DIRS "include"
                         REQUIRES hidparse
                         PRIV_REQUIRES espressif__usb)
else()
  idf_component_register(SRCS "usb_bridge_none.cpp"
                         INCLUDE_DIRS "include"
                         REQUIRES hidparse)
endif()
