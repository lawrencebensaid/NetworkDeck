#include "settings.hpp"

#include <cstring>
#include <mutex>
#include <vector>

#include "esp_log.h"
#include "nvs.h"

namespace settings {
namespace {

constexpr char TAG[] = "settings";
constexpr char kNs[] = "deck32";

std::mutex g_mutex;
Network g_net;
Wifi g_wifi;
int g_brightness = 70;
std::string g_hostname;
bool g_matter = false;
int g_rest = -1;  // the REST API connector: -1 never set (on if API keys exist, see web)

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
    uint8_t wifi_on = 1;
    nvs_get_u8(h, "wifi_on", &wifi_on);
    g_wifi.enabled = wifi_on != 0;
    g_hostname = get_str(h, "host");
    uint8_t matter = 0;
    nvs_get_u8(h, "matter", &matter);
    g_matter = matter != 0;
    g_wifi.ssid = get_str(h, "ssid");
    g_wifi.password = get_str(h, "pass");
    uint8_t b;
    if (nvs_get_u8(h, "bright", &b) == ESP_OK && b <= 100) g_brightness = b;
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
    nvs_set_u8(h, "wifi_on", w.enabled ? 1 : 0);
    nvs_set_str(h, "ssid", w.ssid.c_str());
    nvs_set_str(h, "pass", w.password.c_str());
    nvs_commit(h);
    nvs_close(h);
}

int deck_brightness() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_brightness;
}

void set_deck_brightness(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_brightness = percent;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "bright", static_cast<uint8_t>(percent));
    nvs_commit(h);
    nvs_close(h);
}

std::string hostname() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_hostname;
}

bool valid_hostname(const std::string& h) {
    if (h.empty() || h.size() > 63 || h.front() == '-' || h.back() == '-') return false;
    for (char c : h)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

void set_hostname(const std::string& host) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hostname = host;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "host", host.c_str());
    nvs_commit(h);
    nvs_close(h);
}

bool matter_enabled() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_matter;
}

void set_matter_enabled(bool on) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_matter = on;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "matter", on ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

int rest_api() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_rest < 0) {
        nvs_handle_t h;
        uint8_t v = 0;
        if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_u8(h, "rest", &v) == ESP_OK) g_rest = v ? 1 : 0;
            nvs_close(h);
        }
    }
    return g_rest;
}

void set_rest_api(bool on) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_rest = on ? 1 : 0;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "rest", on ? 1 : 0);
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
    g_brightness = 70;
    g_hostname.clear();
    g_matter = false;
    ESP_LOGW(TAG, "factory reset: network and Wi-Fi settings erased");
}

}  // namespace settings
