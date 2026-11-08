// USB host side of the dock: one Elgato Stream Deck on the P4's high-speed port, as raw HID.
// Reports pass through untouched; the dock never decodes images.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace usb_bridge {

struct DeckInfo {
    uint16_t vid = 0;
    uint16_t pid = 0;
    std::string serial;
    uint16_t input_size = 0;   // bytes per interrupt IN report
    uint16_t output_size = 0;  // bytes per interrupt OUT report
};

struct Callbacks {
    // All three run on the USB client task. Keep them short.
    std::function<void(const DeckInfo&)> on_attach;
    std::function<void()> on_detach;
    std::function<void(const uint8_t* report, size_t len)> on_input;
};

esp_err_t start(const Callbacks& callbacks);

bool attached();
DeckInfo info();

// Queue an interrupt OUT report. Blocks up to `wait` while the queue is full, which is how the
// dock pushes back on the host (the socket stops being read). False if detached or timed out.
bool write(const uint8_t* data, size_t len, TickType_t wait);

// Feature reports over control transfers. Blocking; never call them from the callbacks above.
esp_err_t set_feature(const uint8_t* data, size_t len);
esp_err_t get_feature(uint8_t report_id, std::vector<uint8_t>& out);

}  // namespace usb_bridge
