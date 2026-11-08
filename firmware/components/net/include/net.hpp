// Network for the dock. Milestones 1-2: Ethernet with DHCP, plus the mDNS announcement.
// Wi-Fi, the setup hotspot and static IP come in milestone 5.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "esp_err.h"

namespace net {

esp_err_t start_ethernet();

// Ethernet MAC (also the dock's identity: serial and mDNS name derive from it).
std::array<uint8_t, 6> mac();

// Dotted IPv4 address, empty until DHCP has answered.
std::string ip();

// `_elg._tcp` on `port` with TXT dt=215 (Network Dock) and sn=<serial>.
esp_err_t start_mdns(const std::string& name, const std::string& serial, uint16_t port);

}  // namespace net
