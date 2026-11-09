cmake_minimum_required(VERSION 3.22)

# The app version (esp_app_desc, web interface, OTA prompts): CI passes the exact build version in
# NETWORKDECK_VERSION (0.2.0, 0.2.0-beta.12, 0.2.0-pr.4.57).
# Local builds are VERSION-dev+<commit> (0.2.0-dev+3f9c2a1), so a dock running one says so.
if(DEFINED ENV{NETWORKDECK_VERSION})
  set(PROJECT_VER "$ENV{NETWORKDECK_VERSION}")
else()
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" base LIMIT_COUNT 1)
  execute_process(COMMAND git rev-parse --short=7 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                  OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE git_failed)
  if(git_failed OR commit STREQUAL "")
    set(PROJECT_VER "${base}-dev")
  else()
    set(PROJECT_VER "${base}-dev+${commit}")
  endif()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/esp_matter.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# What Matter controllers show as the maker and model (Basic Information cluster).
idf_build_set_property(COMPILE_DEFINITIONS "CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME=\"NetworkDeck\"" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME=\"NetworkDeck\"" APPEND)
project(deck32)
