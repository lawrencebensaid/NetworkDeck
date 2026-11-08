// Deck32: Stream Deck Network Dock emulator for the Waveshare ESP32-P4-NANO.
//
// Wiring only: usb_bridge (the deck) <-> dock_server (the Stream Deck app) while a host is connected,
// deck_ui (the dock's own screens) otherwise; net, settings, web and OTA around them.

#include <cstdio>
#include <string>

#include "deck_ui/deck.hpp"
#include "dock_server.hpp"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "net.hpp"
#include "nvs_flash.h"
#include "settings.hpp"
#include "usb_bridge.hpp"
#include "web.hpp"

namespace {

constexpr char TAG[] = "deck32";
constexpr char kFirmware[] = "1.00.000";  // what the dock reports to the Stream Deck app

std::string g_name, g_serial;
QueueHandle_t g_actions = nullptr;  // deck_ui events handled off the deck_ui task (they may prompt)

std::string hex(const std::array<uint8_t, 6>& mac, size_t from) {
    std::string s;
    char b[3];
    for (size_t i = from; i < mac.size(); ++i) {
        std::snprintf(b, sizeof(b), "%02X", mac[i]);
        s += b;
    }
    return s;
}

deck_ui::Ip4 to_ui(const settings::Ip4& ip) { return {ip[0], ip[1], ip[2], ip[3]}; }
settings::Ip4 from_ui(const deck_ui::Ip4& ip) { return {ip[0], ip[1], ip[2], ip[3]}; }

void publish_net(const net::Status& s) {
    {
        unsigned a = 0, b = 0, c = 0, d = 0;
        std::array<uint8_t, 4> ip{};
        if (std::sscanf(s.ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
            ip = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d)};
        const auto cfg = settings::network();
        dock_server::set_network(ip, cfg.dhcp ? std::array<uint8_t, 4>{255, 255, 255, 0} : cfg.mask);
    }
    const auto cfg = settings::network();
    deck_ui::NetInfo n;
    n.link = net::link_name(s.link);
    n.ip = s.link == net::Link::Hotspot ? std::string() : s.ip;
    n.firmware = kFirmware;
    n.dhcp = cfg.dhcp;
    n.ip4 = to_ui(cfg.ip);
    n.mask = to_ui(cfg.mask);
    n.gateway = to_ui(cfg.gateway);
    deck_ui::set_net(n);
    deck_ui::Hotspot h;
    h.active = s.link == net::Link::Hotspot;
    h.ssid = s.hotspot_ssid;
    h.password = s.hotspot_password;
    h.url = s.hotspot_ip;
    deck_ui::set_hotspot(h);
}

void apply_network(const settings::Network& n) {
    settings::set_network(n);
    net::apply(n);
    publish_net(net::status());
}

// A confirmation on the deck. While the Stream Deck app is connected, its traffic to the deck is held
// back for the prompt, and the deck is virtually replugged afterwards so the app draws again.
bool ask(const std::string& question) {
    const bool host = dock_server::host_connected();
    if (host) dock_server::set_paused(true);
    const bool yes = deck_ui::confirm(question);
    dock_server::set_paused(false);
    return yes;
}

void actions_task(void*) {
    deck_ui::UiEvent ev;
    for (;;) {
        if (xQueueReceive(g_actions, &ev, portMAX_DELAY) != pdTRUE) continue;
        switch (ev.kind) {
            case deck_ui::UiEvent::Kind::SetDhcp: {
                auto n = settings::network();
                n.dhcp = true;
                apply_network(n);
                break;
            }
            case deck_ui::UiEvent::Kind::SetStatic: {
                settings::Network n;
                n.dhcp = false;
                n.ip = from_ui(ev.ip);
                n.mask = from_ui(ev.mask);
                n.gateway = from_ui(ev.gateway);
                apply_network(n);
                break;
            }
            case deck_ui::UiEvent::Kind::FactoryReset:
                if (ask("Factory reset?")) {
                    settings::factory_reset();
                    vTaskDelay(pdMS_TO_TICKS(300));
                    esp_restart();
                }
                break;
            default:
                break;
        }
    }
}

std::string status_json() {
    const auto s = net::status();
    const auto cfg = settings::network();
    const auto deck = usb_bridge::attached() ? usb_bridge::info() : usb_bridge::DeckInfo{};
    auto ip = [](const settings::Ip4& a) {
        char b[16];
        std::snprintf(b, sizeof(b), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
        return std::string(b);
    };
    char deck_name[48] = "";
    if (usb_bridge::attached()) {
        const auto* m = deck_ui::find_model(deck.pid);
        std::snprintf(deck_name, sizeof(deck_name), "%s", m ? m->name : "Elgato device");
    }
    std::string j = "{";
    j += "\"name\":\"" + g_name + "\",\"serial\":\"" + g_serial + "\",\"firmware\":\"" + kFirmware + "\"";
    j += ",\"build\":\"" + std::string(esp_app_get_description()->version) + "\"";
    j += ",\"link\":\"" + std::string(net::link_name(s.link)) + "\",\"ip\":\"" + s.ip + "\"";
    j += ",\"wifi_available\":" + std::string(s.wifi_available ? "true" : "false");
    j += ",\"wifi_ssid\":\"" + s.wifi_ssid + "\"";
    j += ",\"dhcp\":" + std::string(cfg.dhcp ? "true" : "false");
    j += ",\"static_ip\":\"" + ip(cfg.ip) + "\",\"static_mask\":\"" + ip(cfg.mask) + "\",\"static_gateway\":\"" + ip(cfg.gateway) + "\"";
    j += ",\"host\":" + std::string(dock_server::host_connected() ? "true" : "false");
    j += ",\"deck\":\"" + std::string(deck_name) + "\"}";
    return j;
}

}  // namespace

extern "C" void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    settings::load();

    const auto mac = net::mac();
    cora::DockIdentity identity;
    identity.mac = mac;
    identity.serial = "D3" + hex(mac, 0);  // 14 characters
    identity.firmware = kFirmware;
    g_serial = identity.serial;
    g_name = "Deck32-" + hex(mac, 4);
    ESP_LOGI(TAG, "%s, serial %s, firmware %s, build %s", g_name.c_str(), g_serial.c_str(), kFirmware,
             esp_app_get_description()->version);

    g_actions = xQueueCreate(4, sizeof(deck_ui::UiEvent));
    xTaskCreatePinnedToCore(actions_task, "actions", 4096, nullptr, 4, nullptr, 1);

    deck_ui::Callbacks ui_cb;
    ui_cb.on_event = [](const deck_ui::UiEvent& ev) { xQueueSend(g_actions, &ev, 0); };
    ui_cb.on_replug = [] { dock_server::replug(); };
    deck_ui::start(ui_cb);

    ESP_ERROR_CHECK(net::start(g_name, publish_net));
    publish_net(net::status());

    dock_server::Config config;
    config.identity = identity;
    config.on_host_change = [](bool connected) {
        ESP_LOGI(TAG, "host %s", connected ? "connected" : "disconnected");
        deck_ui::set_host(connected);
    };
    dock_server::start(config);

    usb_bridge::Callbacks cb;
    cb.on_attach = [](const usb_bridge::DeckInfo& info) {
        dock_server::deck_attached(info);
        deck_ui::deck_attached(info.pid);
    };
    cb.on_detach = [] {
        ESP_LOGI(TAG, "deck unplugged");
        deck_ui::deck_detached();
        dock_server::deck_detached();
    };
    cb.on_input = [](const uint8_t* report, size_t len) {
        if (deck_ui::owns_deck()) {
            deck_ui::input(report, len);
        } else {
            dock_server::deck_input(report, len);
        }
    };
    ESP_ERROR_CHECK(usb_bridge::start(cb));

    ESP_ERROR_CHECK(net::start_mdns(g_name, identity.serial, cora::kDockPort));

    web::Hooks hooks;
    hooks.status_json = status_json;
    hooks.scan = [] {
        std::vector<web::Network> out;
        for (const auto& r : net::scan()) out.push_back({r.ssid, r.rssi, r.secure});
        return out;
    };
    hooks.trace = dock_server::trace;
    hooks.key_image = deck_ui::key_image;
    hooks.confirm = ask;
    hooks.set_wifi = [](const settings::Wifi& w) {
        settings::set_wifi(w);
        net::join(w);
    };
    hooks.set_network = apply_network;
    ESP_ERROR_CHECK(web::start(hooks));

    // The new image started its network and services: keep it (otherwise the bootloader rolls back).
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware marked valid");
    }
}
