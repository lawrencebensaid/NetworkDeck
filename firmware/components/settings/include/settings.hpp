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
int deck_brightness();
void set_deck_brightness(int percent);

// Erases network and Wi-Fi settings.
void factory_reset();

}  // namespace settings
