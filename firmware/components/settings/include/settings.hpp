// Stored settings (NVS namespace "deck32"), with defaults and a factory reset.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace settings {

using Ip4 = std::array<uint8_t, 4>;

struct Network {
    bool dhcp = true;
    Ip4 ip{192, 168, 1, 50};
    Ip4 mask{255, 255, 255, 0};
    Ip4 gateway{192, 168, 1, 1};
};

struct Wifi {
    bool enabled = true;   // off: the radio stays off (no Wi-Fi, no setup hotspot)
    std::string ssid;      // empty: no saved network
    std::string password;
};

// Call once after nvs_flash_init().
void load();

Network network();
void set_network(const Network& n);

Wifi wifi();
void set_wifi(const Wifi& w);

// Brightness of the dock's own screens on the deck, 0..100 (the Stream Deck app sets its own).
// How the web interface draws a deck: "black", "white", or empty when not chosen. Per deck serial
// (USB doesn't tell a deck's colour), at most 8 decks remembered.
std::string deck_finish(const std::string& serial);
void set_deck_finish(const std::string& serial, const std::string& finish);

int deck_brightness();
void set_deck_brightness(int percent);

// The mDNS / DHCP hostname, without ".local". Empty: the default (the dock's name, lower case).
std::string hostname();
void set_hostname(const std::string& h);
// A valid hostname label: 1..63 of a-z, 0-9 and '-', not starting or ending with '-'.
bool valid_hostname(const std::string& h);

// Matter was set up (it runs from the next start, and from now on).
bool matter_enabled();
void set_matter_enabled(bool on);

// Erases network and Wi-Fi settings.
void factory_reset();

}  // namespace settings
