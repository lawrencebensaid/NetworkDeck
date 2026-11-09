// Deck32: Stream Deck Network Dock emulator for the Waveshare ESP32-P4-NANO.
//
// Wiring only: usb_bridge (the deck) <-> dock_server (the Stream Deck app) while a host is connected,
// deck_ui (the dock's own screens) otherwise; net, settings, web and OTA around them.

#include <cctype>
#include <cstdio>
#include <string>

#include "deck_ui/deck.hpp"
#include "dock_server.hpp"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "matter.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "net.hpp"
#include "nvs_flash.h"
#include "settings.hpp"
#include "tls.h"
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
    // A DHCP address is named in the HTTPS certificate from the next start (static ones always are).
    if (s.link == net::Link::Ethernet || s.link == net::Link::Wifi) tls_set_ip4(s.ip.c_str());
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
                    // Matter's own storage goes too: it leaves every controller and restarts by itself.
                    if (matter::status().running && matter::unpair() == ESP_OK) vTaskDelay(pdMS_TO_TICKS(5000));
                    esp_restart();
                }
                break;
            default:
                break;
        }
    }
}

std::string json_escape(const std::string& in) {
    std::string o;
    for (char c : in) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) o += c;
    }
    return o;
}

// The hostname: the one set in the web interface, or the dock's name in lower case (deck32-204c).
std::string hostname() {
    std::string h = settings::hostname();
    if (!h.empty()) return h;
    for (char c : g_name) h.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return h;
}

unsigned deck_keys() {
    if (!usb_bridge::attached()) return 15;
    const auto* m = deck_ui::find_model(usb_bridge::info().pid);
    return m ? static_cast<unsigned>(m->keys()) : 15;
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
    j += ",\"wifi_enabled\":" + std::string(s.wifi_enabled ? "true" : "false");
    j += ",\"wifi_ssid\":\"" + json_escape(s.wifi_ssid) + "\"";
    j += ",\"wifi_rssi\":" + std::to_string(net::wifi_rssi());
    j += ",\"hostname\":\"" + net::hostname() + "\"";
    j += ",\"dhcp\":" + std::string(cfg.dhcp ? "true" : "false");
    j += ",\"static_ip\":\"" + ip(cfg.ip) + "\",\"static_mask\":\"" + ip(cfg.mask) + "\",\"static_gateway\":\"" + ip(cfg.gateway) + "\"";
    j += ",\"host\":" + std::string(dock_server::host_connected() ? "true" : "false");
    j += ",\"deck\":\"" + std::string(deck_name) + "\"}";
    return j;
}

std::string matter_json() {
    const auto m = matter::status();
    std::string j = "{\"enabled\":" + std::string(settings::matter_enabled() ? "true" : "false");
    j += ",\"running\":" + std::string(m.running ? "true" : "false");
    j += ",\"fabrics\":" + std::to_string(m.fabrics);
    j += ",\"window_open\":" + std::string(m.window_open ? "true" : "false");
    j += ",\"window_seconds\":" + std::to_string(m.window_seconds);
    j += ",\"qr\":\"" + m.qr + "\",\"manual\":\"" + m.manual + "\"}";  // base38 and digits: nothing to escape
    return j;
}

// Leaving every controller restarts the dock: give the web reply a moment to go out first.
void matter_off_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (matter::unpair() != ESP_OK) esp_restart();  // not running: nothing to leave, start clean
    vTaskDelete(nullptr);
}

}  // namespace

// What the HTTPS certificate names (components/tls): the hostname, and the static address if any.
extern "C" void tls_host_hostname(char* out, size_t len) { std::snprintf(out, len, "%s", hostname().c_str()); }
extern "C" bool tls_host_static_ip(char* out, size_t len) {
    const auto n = settings::network();
    if (n.dhcp) return false;
    std::snprintf(out, len, "%u.%u.%u.%u", n.ip[0], n.ip[1], n.ip[2], n.ip[3]);
    return true;
}

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
    ui_cb.on_key = [](int key, bool down) { matter::key(static_cast<unsigned>(key), down); };
    deck_ui::start(ui_cb);
    deck_ui::set_brightness(settings::deck_brightness());

    ESP_ERROR_CHECK(net::start(g_name, publish_net));
    net::set_hostname(hostname());
    publish_net(net::status());

    dock_server::Config config;
    config.identity = identity;
    config.on_host_change = [](bool connected) {
        ESP_LOGI(TAG, "host %s", connected ? "connected" : "disconnected");
        deck_ui::set_host(connected);
    };
    config.on_host_output = deck_ui::host_output;
    config.on_host_feature = deck_ui::host_feature;
    dock_server::start(config);

    usb_bridge::Callbacks cb;
    cb.on_attach = [](const usb_bridge::DeckInfo& info) {
        dock_server::deck_attached(info);
        deck_ui::deck_attached(info.pid);
        if (const auto* m = deck_ui::find_model(info.pid)) matter::remember_keys(static_cast<unsigned>(m->keys()));
    };
    cb.on_detach = [] {
        ESP_LOGI(TAG, "deck unplugged");
        deck_ui::deck_detached();
        dock_server::deck_detached();
    };
    cb.on_input = [](const uint8_t* report, size_t len) {
        deck_ui::observe_input(report, len);
        if (deck_ui::owns_deck()) {
            deck_ui::input(report, len);
        } else {
            dock_server::deck_input(report, len);
        }
    };
    ESP_ERROR_CHECK(usb_bridge::start(cb));

    ESP_ERROR_CHECK(net::start_mdns(g_name, identity.serial, cora::kDockPort));

    // Matter (Home Assistant, Apple Home): off until it is set up in the web interface. A dock that
    // ran Matter before it was opt-in has a setup code: it keeps running it.
    if (!settings::matter_enabled() && matter::has_setup_code()) settings::set_matter_enabled(true);
    if (settings::matter_enabled())
        if (esp_err_t err = matter::start(deck_keys(), g_name); err != ESP_OK) ESP_LOGE(TAG, "Matter did not start: %s", esp_err_to_name(err));

    web::Hooks hooks;
    hooks.status_json = status_json;
    hooks.hostname = [] { return net::hostname(); };
    hooks.deck_json = deck_ui::deck_json;
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
    hooks.set_brightness = [](int percent) {
        settings::set_deck_brightness(percent);
        deck_ui::set_brightness(percent);
    };
    hooks.matter_json = matter_json;
    hooks.matter_open = [] { return matter::open_window() == ESP_OK; };
    hooks.matter_setup = [] {
        settings::set_matter_enabled(true);
        if (matter::status().running) return true;
        const esp_err_t err = matter::start(deck_keys(), g_name);
        if (err != ESP_OK) ESP_LOGE(TAG, "Matter did not start: %s", esp_err_to_name(err));
        return err == ESP_OK;
    };
    hooks.matter_off = [] {
        settings::set_matter_enabled(false);
        return xTaskCreate(matter_off_task, "matter_off", 4096, nullptr, 5, nullptr) == pdPASS;
    };
    hooks.set_hostname = [](const std::string& h) {
        settings::set_hostname(h);
        net::set_hostname(h.empty() ? hostname() : h);
    };
    ESP_ERROR_CHECK(web::start(hooks));

    // The new image started its network and services: keep it (otherwise the bootloader rolls back).
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware marked valid");
    }
}
