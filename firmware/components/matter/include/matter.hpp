// Matter: the dock as a Matter device, so Home Assistant, Apple Home and others can use its keys.
//
// Every key on the deck is a Generic Switch endpoint (endpoint 1 is the top-left key, numbered the
// way the deck reads). A press sends InitialPress, then ShortRelease, or LongPress after 600 ms and
// LongRelease. Home Assistant shows each key as an event entity for automations.
//
// Matter runs over the network that components/net brings up (Ethernet, or Wi-Fi without a cable):
// commissioning is on-network, with a QR code or an 11-digit code. The code is random per dock, made
// on first start and kept in NVS. Attestation uses the SDK's test certificates (an uncertified
// device: controllers ask before adding it).
#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"

namespace matter {

struct Status {
    bool running = false;
    unsigned fabrics = 0;        // controllers it has been added to
    bool window_open = false;    // can be added to a controller now
    unsigned window_seconds = 0; // how long it stays open
    std::string qr;              // "MT:..." for a QR code
    std::string manual;          // the 11-digit pairing code
};

// Starts the Matter stack. Call after the network and mDNS are up. It has one switch per key of the
// deck last plugged in (see remember_keys), or `default_keys` before any deck was seen.
esp_err_t start(unsigned default_keys, const std::string& node_label);

// The plugged-in deck has `keys` keys. A different count than the running one takes effect at the
// next start (controllers see the endpoints change then).
void remember_keys(unsigned keys);

// A key went down or up (index from 0). Safe from any task; does nothing before start().
void key(unsigned index, bool down);

Status status();

// Lets a new controller add the dock for 15 minutes.
esp_err_t open_window();

// Leaves every controller (removes all fabrics) and erases Matter's storage and the setup code.
// Restart afterwards: the stack still runs until then.
esp_err_t unpair();

// A setup code was made on this dock (Matter was set up before; the code is kept until unpair()).
bool has_setup_code();

}  // namespace matter
