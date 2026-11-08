// TCP side of the dock: the dock connection (5343) and the deck connection (5344), one host at a time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "cora/protocol.hpp"
#include "usb_bridge.hpp"

namespace dock_server {

struct Config {
    cora::DockIdentity identity;
    // Fires when the first host connects (true) and when the last of its connections closes (false).
    std::function<void(bool connected)> on_host_change;
};

void start(const Config& config);

bool host_connected();

// Wire these to usb_bridge's callbacks.
void deck_attached(const usb_bridge::DeckInfo& info);
void deck_detached();
void deck_input(const uint8_t* report, size_t len);

}  // namespace dock_server
