// API keys: see auth.hpp.

#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

#include "auth.hpp"
#include "esp_random.h"
#include "nvs.h"
#include "psa/crypto.h"

namespace auth {
namespace {

constexpr char kNs[] = "deck32";
constexpr char kKey[] = "apikeys";
constexpr char kPrefix[] = "ndk_";

struct Stored {
    char id[13];  // 12 hex
    char name[33];
    uint8_t hash[32];  // SHA-256 of the secret
    uint32_t created;  // Unix time, 0 if the clock wasn't set
};

std::mutex g_mutex;
std::vector<Stored> g_keys;
std::vector<uint32_t> g_last_used;  // per key, since this start (not stored: it would wear the flash)
bool g_loaded = false;

void load_locked() {
    if (g_loaded) return;
    g_loaded = true;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = 0;
    if (nvs_get_blob(h, kKey, nullptr, &len) == ESP_OK && len % sizeof(Stored) == 0 && len <= kMaxApiKeys * sizeof(Stored)) {
        g_keys.resize(len / sizeof(Stored));
        if (nvs_get_blob(h, kKey, g_keys.data(), &len) != ESP_OK) g_keys.clear();
    }
    nvs_close(h);
    g_last_used.assign(g_keys.size(), 0);
}

bool save_locked() {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = g_keys.empty() ? nvs_erase_key(h, kKey) : nvs_set_blob(h, kKey, g_keys.data(), g_keys.size() * sizeof(Stored));
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

std::array<uint8_t, 32> sha256(const std::string& s) {
    std::array<uint8_t, 32> out{};
    size_t len = 0;
    psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(s.data()), s.size(), out.data(), out.size(), &len);
    return out;
}

std::string hex(const uint8_t* b, size_t n) {
    static const char kHex[] = "0123456789abcdef";
    std::string o;
    for (size_t i = 0; i < n; ++i) {
        o += kHex[b[i] >> 4];
        o += kHex[b[i] & 15];
    }
    return o;
}

uint32_t now() {
    const time_t t = time(nullptr);
    return t > 1700000000 ? static_cast<uint32_t>(t) : 0;
}

}  // namespace

std::vector<ApiKey> api_keys() {
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    std::vector<ApiKey> out;
    for (size_t i = 0; i < g_keys.size(); ++i) out.push_back({g_keys[i].id, g_keys[i].name, g_keys[i].created, g_last_used[i]});
    return out;
}

std::string api_key_create(const std::string& name, std::string& id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    if (name.empty() || name.size() > 32 || g_keys.size() >= kMaxApiKeys) return {};
    uint8_t secret[20], idb[6];
    esp_fill_random(secret, sizeof(secret));
    esp_fill_random(idb, sizeof(idb));
    const std::string key = std::string(kPrefix) + hex(secret, sizeof(secret));
    Stored s{};
    std::snprintf(s.id, sizeof(s.id), "%s", hex(idb, sizeof(idb)).c_str());
    std::snprintf(s.name, sizeof(s.name), "%s", name.c_str());
    const auto h = sha256(key);
    std::memcpy(s.hash, h.data(), h.size());
    s.created = now();
    g_keys.push_back(s);
    g_last_used.push_back(0);
    if (!save_locked()) {
        g_keys.pop_back();
        g_last_used.pop_back();
        return {};
    }
    id = s.id;
    return key;
}

bool api_key_remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    for (size_t i = 0; i < g_keys.size(); ++i) {
        if (id == g_keys[i].id) {
            g_keys.erase(g_keys.begin() + static_cast<long>(i));
            g_last_used.erase(g_last_used.begin() + static_cast<long>(i));
            return save_locked();
        }
    }
    return false;
}

bool api_key_check(const std::string& secret, ApiKey& who) {
    if (secret.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return false;
    const auto h = sha256(secret);
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    for (size_t i = 0; i < g_keys.size(); ++i) {
        uint8_t diff = 0;  // constant time
        for (size_t b = 0; b < h.size(); ++b) diff |= static_cast<uint8_t>(h[b] ^ g_keys[i].hash[b]);
        if (diff == 0) {
            const bool first = g_last_used[i] == 0;
            g_last_used[i] = now() ? now() : 1;
            who = {g_keys[i].id, g_keys[i].name, g_keys[i].created, first ? 0u : g_last_used[i]};
            return true;
        }
    }
    return false;
}

}  // namespace auth
