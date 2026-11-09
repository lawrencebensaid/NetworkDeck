// Network for the dock: Ethernet first, Wi-Fi (through the C6) when there's no Ethernet link,
// and the setup hotspot when there's no Wi-Fi to join. Plus the mDNS announcement.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "esp_err.h"
#include "settings.hpp"

namespace net {

enum class Link { None, Ethernet, Wifi, Hotspot };
const char* link_name(Link l);
// The chip the firmware runs on: "ESP32-P4", "ESP32-C6".
const char* chip_name();

struct Status {
    Link link = Link::None;
    std::string ip;              // address on the active link, empty while there is none
    bool wifi_available = false; // the C6 answered
    bool wifi_enabled = true;    // the Wi-Fi setting (off: no Wi-Fi and no hotspot)
    std::string wifi_ssid;       // the saved network
    std::string hotspot_ssid, hotspot_password;  // set while the hotspot is up
    std::string hotspot_ip;
};

struct ScanResult {
    std::string ssid;
    int rssi;
    bool secure;
};

// Starts Ethernet and Wi-Fi and runs the link policy. `on_change` runs on the net task.
esp_err_t start(const std::string& name, std::function<void(const Status&)> on_change);

Status status();

// Static or DHCP on Ethernet and Wi-Fi.
void apply(const settings::Network& n);
// Join this network now (the caller has stored it).
void join(const settings::Wifi& w);
std::vector<ScanResult> scan();
// Holds the setup hotspot open for 15 minutes, also while Ethernet is up (asked for on the deck);
// false closes it again (unless the dock has no other network).
void open_hotspot(bool open);

// Ethernet MAC (also the dock's identity: serial and mDNS name derive from it).
std::array<uint8_t, 6> mac();
std::string ip();

// `_elg._tcp` on `port` with TXT dt=215 (Network Dock) and sn=<serial>.
esp_err_t start_mdns(const std::string& name, const std::string& serial, uint16_t port);

// The hostname for mDNS (<host>.local) and DHCP. Call after start(); mDNS takes it at once, DHCP at
// the next lease.
void set_hostname(const std::string& host);
std::string hostname();

// A computer on the network by its address: the name the network's DNS server has for it (the router,
// from its DHCP lease: "MacBookPro"), else its own NetBIOS name (Windows, and Macs with file sharing:
// "MAC-MINI"). `hostname` is the full DNS name. Empty when neither answers.
// Blocks for up to two seconds: call it off the network task.
struct Peer {
    std::string name, hostname;
};
Peer describe(const std::string& ip);

// Signal of the joined Wi-Fi network in dBm, 0 when not on Wi-Fi.
int wifi_rssi();


}  // namespace net
