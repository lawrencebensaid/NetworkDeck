// The dock's web interface: status, Wi-Fi setup, network settings, firmware upload and the protocol
// trace. Viewing is open; every change is confirmed on the deck, except from the setup hotspot.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "esp_err.h"
#include "settings.hpp"

namespace web {

struct Network {
    std::string ssid;
    int rssi;
    bool secure;
};

struct Hooks {
    std::function<std::string()> status_json;               // GET /api/status
    std::function<std::vector<Network>()> scan;              // GET /api/scan
    std::function<std::string()> trace;                      // GET /trace
    // GET /api/key?n=<key>: the image on that key, and its content type
    std::function<std::vector<uint8_t>(int key, std::string& type)> key_image;
    // Ask on the deck; returns true when confirmed. Not called for requests from the hotspot.
    std::function<bool(const std::string& question)> confirm;
    std::function<void(const settings::Wifi&)> set_wifi;     // after confirmation
    std::function<void(const settings::Network&)> set_network;
};

esp_err_t start(const Hooks& hooks);

}  // namespace web
