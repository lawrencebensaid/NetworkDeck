// usb_bridge on a chip without a USB host controller (ESP32-C6, ESP32-C5): no Stream Deck can be
// attached. The rest of the firmware (network, web interface, log) runs as usual.

#include "usb_bridge.hpp"

#include "esp_log.h"

namespace usb_bridge {

esp_err_t start(const Callbacks&) {
    ESP_LOGW("usb", "this chip has no USB host: no Stream Deck can be attached");
    return ESP_OK;
}
bool attached() { return false; }
DeckInfo info() { return {}; }
bool write(const uint8_t*, size_t, TickType_t) { return false; }
esp_err_t set_feature(const uint8_t*, size_t) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_feature(uint8_t, std::vector<uint8_t>&) { return ESP_ERR_NOT_SUPPORTED; }

}  // namespace usb_bridge
