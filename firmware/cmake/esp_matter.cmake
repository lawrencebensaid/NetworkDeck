# esp-matter (Espressif's Matter SDK) as a component, fetched once into third_party/esp_matter.
#
# The release archive is 10 MB (53 MB unpacked), too large to keep in git, so it is downloaded at
# configure time and checked against a pinned hash, then patched so Matter shares the network with the
# rest of the firmware instead of owning it:
#
# - Its manifest pins esp_hosted ~2.7 and esp_wifi_remote ^0.7 for the P4, but this board's ESP32-C5
#   runs esp-hosted 2.12.9 and the host side must match (components/net pins those). Pins dropped.
# - ESPEthernetDriver::Init installs a second Ethernet driver on the same MAC. components/net owns
#   Ethernet (and adds the IPv6 link-local address Matter needs), so Init does nothing.
# - Matter's DNS-SD sets the mDNS hostname to its MAC-derived name, which would replace
#   deck32-xxxx.local. It keeps ours instead (controllers follow the SRV record, any name works), and
#   adds its subtypes (_L<discriminator>, _CM, ...) to our host.
# - Its CMakeLists leaves out the IDF-mDNS DNS-SD backend when Matter has no Wi-Fi of its own (it
#   assumes the built-in minimal mDNS then). Here it is the backend whenever minimal mDNS is off.
#
# Changing a patch: bump ESP_MATTER_PATCHES so existing checkouts extract and patch again.

set(ESP_MATTER_VERSION "1.6.1")
set(ESP_MATTER_SHA256 "b0b6d86c490be2e90864952e0dede0f51c652db9ceb4766a1f1cfe87b70532e0")
set(ESP_MATTER_URL "https://components-file.espressif.com/components/espressif/esp_matter/${ESP_MATTER_VERSION}/espressif__esp_matter-v${ESP_MATTER_VERSION}.zip")
set(ESP_MATTER_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/esp_matter")
set(ESP_MATTER_PATCHES 2)
set(ESP_MATTER_STAMP "${ESP_MATTER_DIR}/.networkdeck-${ESP_MATTER_VERSION}-p${ESP_MATTER_PATCHES}")

if(NOT EXISTS "${ESP_MATTER_STAMP}")
  set(zip "${CMAKE_CURRENT_LIST_DIR}/../third_party/esp_matter-${ESP_MATTER_VERSION}.zip")
  if(NOT EXISTS "${zip}")
    message(STATUS "Downloading esp-matter ${ESP_MATTER_VERSION}")
    file(DOWNLOAD "${ESP_MATTER_URL}" "${zip}" EXPECTED_HASH SHA256=${ESP_MATTER_SHA256} STATUS st)
    list(GET st 0 code)
    if(NOT code EQUAL 0)
      file(REMOVE "${zip}")
      message(FATAL_ERROR "Could not download esp-matter: ${st}")
    endif()
  endif()
  file(REMOVE_RECURSE "${ESP_MATTER_DIR}")
  file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${ESP_MATTER_DIR}")

  file(READ "${ESP_MATTER_DIR}/idf_component.yml" manifest)
  string(REGEX REPLACE "  espressif/esp_hosted:\n    rules:\n    - if: target in \\[esp32p4\\]\n    version: [^\n]*\n" "" manifest "${manifest}")
  string(REGEX REPLACE "  espressif/esp_wifi_remote:\n    rules:\n    - if: target in \\[esp32p4\\]\n    version: [^\n]*\n" "" manifest "${manifest}")
  if(manifest MATCHES "esp_hosted|esp_wifi_remote")
    message(FATAL_ERROR "esp-matter ${ESP_MATTER_VERSION}: its manifest changed, update cmake/esp_matter.cmake")
  endif()
  file(WRITE "${ESP_MATTER_DIR}/idf_component.yml" "${manifest}")

  set(chip "${ESP_MATTER_DIR}/connectedhomeip/connectedhomeip/src/platform/ESP32")
  function(esp_matter_patch file from to)
    file(READ "${file}" text)
    string(FIND "${text}" "${from}" at)
    if(at EQUAL -1)
      message(FATAL_ERROR "esp-matter ${ESP_MATTER_VERSION}: ${file} changed, update cmake/esp_matter.cmake")
    endif()
    string(REPLACE "${from}" "${to}" text "${text}")
    file(WRITE "${file}" "${text}")
  endfunction()

  file(READ "${chip}/NetworkCommissioningDriver_Ethernet.cpp" eth)
  string(FIND "${eth}" "CHIP_ERROR ESPEthernetDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)\n{" begin)
  string(FIND "${eth}" "} // namespace NetworkCommissioning" end)
  if(begin EQUAL -1 OR end EQUAL -1)
    message(FATAL_ERROR "esp-matter ${ESP_MATTER_VERSION}: NetworkCommissioningDriver_Ethernet.cpp changed, update cmake/esp_matter.cmake")
  endif()
  string(SUBSTRING "${eth}" 0 ${begin} head)
  string(SUBSTRING "${eth}" ${end} -1 tail)
  file(WRITE "${chip}/NetworkCommissioningDriver_Ethernet.cpp" "${head}// NetworkDeck: Ethernet is brought up by components/net.\nCHIP_ERROR ESPEthernetDriver::Init(NetworkStatusChangeCallback *)\n{\n    return CHIP_NO_ERROR;\n}\n\n${tail}")

  esp_matter_patch("${chip}/ESP32DnssdImpl.cpp"
    "    if (strcmp(service->mHostName, \"\") != 0)\n    {\n        VerifyOrExit(mdns_hostname_set(service->mHostName) == ESP_OK, error = CHIP_ERROR_INTERNAL);\n    }\n"
    "    // NetworkDeck: keep the dock's own mDNS hostname.\n")
  esp_matter_patch("${chip}/ESP32DnssdImpl.cpp"
    "GetProtocolString(service->mProtocol), service->mHostName,"
    "GetProtocolString(service->mProtocol), nullptr,")
  esp_matter_patch("${ESP_MATTER_DIR}/CMakeLists.txt"
    "if((CONFIG_USE_MINIMAL_MDNS) OR ((NOT CONFIG_ENABLE_WIFI_STATION) AND (NOT CONFIG_ENABLE_WIFI_AP)))"
    "if(CONFIG_USE_MINIMAL_MDNS)")
  file(WRITE "${ESP_MATTER_STAMP}" "")
endif()
