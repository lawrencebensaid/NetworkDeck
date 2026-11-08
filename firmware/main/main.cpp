// Deck32: Stream Deck Network Dock emulator for the Waveshare ESP32-P4-NANO.
// Milestones 1-2: USB bridge, Ethernet + mDNS, dock protocol. While no host is connected the deck
// shows a test pattern and key presses are logged (milestone 4 replaces this with the dock screens).

#include <cstdio>
#include <string>

#include "dock_server.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.hpp"
#include "nvs_flash.h"
#include "usb_bridge.hpp"

namespace {

constexpr char TAG[] = "deck32";
constexpr char kFirmware[] = "1.00.000";
TaskHandle_t g_ui_task = nullptr;

// Gen2 decks with RGB key fill (feature report 03 06 <key> <r> <g> <b>).
int rgb_fill_keys(uint16_t pid) {
    switch (pid) {
        case 0x006d:  // Stream Deck (second generation)
        case 0x0080:  // MK.2
        case 0x00a5:  // MK.2 Scissor
            return 15;
        default:
            return 0;
    }
}

void draw_test_pattern() {
    const auto info = usb_bridge::info();
    const int keys = rgb_fill_keys(info.pid);
    if (keys == 0) {
        ESP_LOGI(TAG, "no test pattern for %04x:%04x", info.vid, info.pid);
        return;
    }
    const uint8_t brightness[] = {0x03, 0x08, 60};
    usb_bridge::set_feature(brightness, sizeof(brightness));
    for (int k = 0; k < keys; ++k) {
        // A hue sweep, so a wrong key order is obvious.
        const int h = k * 255 / keys;
        const uint8_t r = static_cast<uint8_t>(h < 85 ? 255 - h * 3 : h < 170 ? 0 : (h - 170) * 3);
        const uint8_t g = static_cast<uint8_t>(h < 85 ? h * 3 : h < 170 ? 255 - (h - 85) * 3 : 0);
        const uint8_t b = static_cast<uint8_t>(h < 85 ? 0 : h < 170 ? (h - 85) * 3 : 255 - (h - 170) * 3);
        const uint8_t fill[] = {0x03, 0x06, static_cast<uint8_t>(k), r, g, b};
        usb_bridge::set_feature(fill, sizeof(fill));
    }
    ESP_LOGI(TAG, "test pattern drawn on %d keys", keys);
}

// Owns the deck while no host is connected. Woken on attach and when the host leaves.
void ui_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(100));
        if (usb_bridge::attached() && !dock_server::host_connected()) draw_test_pattern();
    }
}

void log_keys(const uint8_t* r, size_t len) {
    // Gen2 button report: 01 00 <count u16> <state per key…>
    static uint8_t last[64] = {};
    if (len < 4 || r[0] != 0x01 || r[1] != 0x00) return;
    const size_t count = std::min<size_t>(r[2] | (r[3] << 8), std::min<size_t>(len - 4, sizeof(last)));
    for (size_t k = 0; k < count; ++k) {
        if (r[4 + k] != last[k]) {
            last[k] = r[4 + k];
            ESP_LOGI(TAG, "key %u %s", static_cast<unsigned>(k), last[k] ? "down" : "up");
        }
    }
}

std::string hex(const std::array<uint8_t, 6>& mac, size_t from) {
    std::string s;
    char b[3];
    for (size_t i = from; i < mac.size(); ++i) {
        std::snprintf(b, sizeof(b), "%02X", mac[i]);
        s += b;
    }
    return s;
}

}  // namespace

extern "C" void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(net::start_ethernet());
    const auto mac = net::mac();

    cora::DockIdentity identity;
    identity.mac = mac;
    identity.serial = "D3" + hex(mac, 0);  // 14 characters
    identity.firmware = kFirmware;
    const std::string name = "Deck32-" + hex(mac, 4);
    ESP_LOGI(TAG, "%s, serial %s, firmware %s", name.c_str(), identity.serial.c_str(), kFirmware);

    xTaskCreatePinnedToCore(ui_task, "ui", 4096, nullptr, 5, &g_ui_task, 1);

    dock_server::Config config;
    config.identity = identity;
    config.on_host_change = [](bool connected) {
        ESP_LOGI(TAG, "host %s", connected ? "connected" : "disconnected");
        if (!connected) xTaskNotifyGive(g_ui_task);
    };
    dock_server::start(config);

    usb_bridge::Callbacks cb;
    cb.on_attach = [](const usb_bridge::DeckInfo& info) {
        dock_server::deck_attached(info);
        xTaskNotifyGive(g_ui_task);
    };
    cb.on_detach = [] {
        ESP_LOGI(TAG, "deck unplugged");
        dock_server::deck_detached();
    };
    cb.on_input = [](const uint8_t* report, size_t len) {
        if (dock_server::host_connected()) {
            dock_server::deck_input(report, len);
        } else {
            log_keys(report, len);
        }
    };
    ESP_ERROR_CHECK(usb_bridge::start(cb));

    ESP_ERROR_CHECK(net::start_mdns(name, identity.serial, cora::kDockPort));
}
