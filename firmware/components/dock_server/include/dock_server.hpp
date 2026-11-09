// TCP side of the dock: the dock connection (5343) and the deck connection (5344), one host at a time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <string>

#include "cora/protocol.hpp"
#include "usb_bridge.hpp"

namespace dock_server {

struct Config {
    cora::DockIdentity identity;
    // Fires when the first host connects (true) and when the last of its connections closes (false).
    std::function<void(bool connected)> on_host_change;
    // Every output report and feature report the host sends the deck (for the web interface's live view).
    std::function<void(const uint8_t* report, size_t len)> on_host_output;
    std::function<void(const uint8_t* report, size_t len)> on_host_feature;
};

void start(const Config& config);

bool host_connected();

// The computer running the Stream Deck app: its address (empty when none) and how long it has been
// connected.
struct Host {
    std::string ip;
    uint32_t seconds = 0;
    std::string name, hostname;  // the computer's name (set_host_info), or empty
};
Host host();
// What is known of the computer at `ip` (looked up when it connects: net::describe). Ignored if another
// host holds the dock by then.
void set_host_info(const std::string& ip, const std::string& name, const std::string& hostname);

// Wire these to usb_bridge's callbacks.
void deck_attached(const usb_bridge::DeckInfo& info);
void deck_detached();
void deck_input(const uint8_t* report, size_t len);

// The dock's current address, reported in the 0x87 network configuration.
void set_network(const std::array<uint8_t, 4>& ip, const std::array<uint8_t, 4>& netmask);

// While paused (a prompt on the deck), traffic from the host to the deck is dropped.
void set_paused(bool paused);
// Virtual unplug and replug of the deck, so the host draws its profile again.
void replug();

// The last protocol frames in and out, oldest first, one per line (for the web interface).
std::string trace();

}  // namespace dock_server
