# The P4's hardware JPEG encoder draws the dock's screens on gen2 decks.
if(IDF_TARGET STREQUAL "esp32p4")
  set(jpeg esp_driver_jpeg)
endif()
idf_component_register(SRCS "src/canvas.cpp" "src/font_data.cpp" "src/font_mono_data.cpp" "src/model.cpp" "src/ui.cpp" "src/deck.cpp"
                            "third_party/qrcodegen.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "third_party"
                       REQUIRES usb_bridge
                       PRIV_REQUIRES ${jpeg} esp_timer)
