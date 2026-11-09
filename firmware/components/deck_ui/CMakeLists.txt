idf_component_register(SRCS "src/canvas.cpp" "src/font_data.cpp" "src/font_mono_data.cpp" "src/model.cpp" "src/ui.cpp" "src/deck.cpp"
                            "third_party/qrcodegen.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "third_party"
                       REQUIRES usb_bridge
                       PRIV_REQUIRES esp_driver_jpeg esp_timer)
