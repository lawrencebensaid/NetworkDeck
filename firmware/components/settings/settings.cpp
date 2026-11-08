#include "settings.hpp"

#include <cstring>
#include <mutex>

#include "esp_log.h"
#include "nvs.h"

namespace settings {
namespace {

constexpr char TAG[] = "settings";
constexpr char kNs[] = "deck32";

std::mutex g_mutex;
Network g_net;
Wifi g_wifi;

uint32_t pack(const Ip4& ip) { return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3]; }
Ip4 unpack(uint32_t v) {
    return {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
}

std::string get_str(nvs_handle_t h, const char* key) {
    size_t len = 0;
    if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) return {};
    std::string s(len, '\0');
    nvs_get_str(h, key, s.data(), &len);
    s.resize(len - 1);
    return s;
}

}  // namespace

void load() {
    std::lock_guard<std::mutex> lock(g_mutex);
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no stored settings, using defaults");
        return;
    }
    uint8_t dhcp = 1;
    uint32_t v;
    nvs_get_u8(h, "dhcp", &dhcp);
    g_net.dhcp = dhcp != 0;
    if (nvs_get_u32(h, "ip", &v) == ESP_OK) g_net.ip = unpack(v);
    if (nvs_get_u32(h, "mask", &v) == ESP_OK) g_net.mask = unpack(v);
    if (nvs_get_u32(h, "gw", &v) == ESP_OK) g_net.gateway = unpack(v);
    g_wifi.ssid = get_str(h, "ssid");
    g_wifi.password = get_str(h, "pass");
    nvs_close(h);
    ESP_LOGI(TAG, "network %s, Wi-Fi %s", g_net.dhcp ? "DHCP" : "static", g_wifi.ssid.empty() ? "not set" : g_wifi.ssid.c_str());
}

Network network() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_net;
}

void set_network(const Network& n) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_net = n;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "dhcp", n.dhcp ? 1 : 0);
    nvs_set_u32(h, "ip", pack(n.ip));
    nvs_set_u32(h, "mask", pack(n.mask));
    nvs_set_u32(h, "gw", pack(n.gateway));
    nvs_commit(h);
    nvs_close(h);
}

Wifi wifi() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_wifi;
}

void set_wifi(const Wifi& w) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_wifi = w;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", w.ssid.c_str());
    nvs_set_str(h, "pass", w.password.c_str());
    nvs_commit(h);
    nvs_close(h);
}

void factory_reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    g_net = Network{};
    g_wifi = Wifi{};
    ESP_LOGW(TAG, "factory reset: network and Wi-Fi settings erased");
}

}  // namespace settings
