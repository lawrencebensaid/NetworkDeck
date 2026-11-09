cmake_minimum_required(VERSION 3.22)

# The app version (esp_app_desc, web interface, OTA prompts): CI passes the exact build version in
# NETWORKDECK_VERSION (0.1.0, 0.1.0-beta.12, ...); local builds use VERSION as it stands.
if(DEFINED ENV{NETWORKDECK_VERSION})
  set(PROJECT_VER "$ENV{NETWORKDECK_VERSION}")
else()
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" PROJECT_VER LIMIT_COUNT 1)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/esp_matter.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# What Matter controllers show as the maker and model (Basic Information cluster).
idf_build_set_property(COMPILE_DEFINITIONS "CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME=\"NetworkDeck\"" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME=\"NetworkDeck\"" APPEND)
project(deck32)
