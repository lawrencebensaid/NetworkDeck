// The ESP side of deck_ui: owns the deck while no host is connected (or while a prompt is shown),
// draws the screens from ui.hpp and turns key presses into settings changes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "deck_ui/ui.hpp"

namespace deck_ui {

struct Callbacks {
    // A change made on the deck (DHCP/static). Runs on the deck_ui task.
    std::function<void(const UiEvent&)> on_event;
    // After a prompt shown while a host was connected: unplug and replug the deck virtually,
    // so the host redraws its profile.
    std::function<void()> on_replug;
};

void start(const Callbacks& cb);

void deck_attached(uint16_t pid);
void deck_detached();
void set_host(bool connected);
// True while deck_ui draws and reads keys (no host, or a prompt is up). Input goes to input() then.
bool owns_deck();
void input(const uint8_t* report, size_t len);

void set_net(const NetInfo& n);
void set_hotspot(const Hotspot& h);

// Asks on the deck and waits (up to 30 s) for ✓ or ✕. False if no deck we can draw on is attached.
bool confirm(const std::string& question);

// ── live view for the web interface ──
// What the host sends the deck (images and features), and every input report, whoever owns the deck.
void host_output(const uint8_t* report, size_t len);
void host_feature(const uint8_t* report, size_t len);
void observe_input(const uint8_t* report, size_t len);
// The image on `key` now (JPEG or BMP, in the deck's orientation), and its content type. Empty if the
// key shows nothing or a plain colour (see deck_json()).
std::vector<uint8_t> key_image(int key, std::string& content_type);
// {"attached","model","cols","rows","key_px","transform","source":"app"|"dock","keys":[{"v","color"}],"pressed":[..]}
std::string deck_json();

}  // namespace deck_ui
