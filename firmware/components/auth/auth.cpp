#include "auth.hpp"

#include <array>
#include <cstring>
#include <mutex>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "psa/crypto.h"

namespace auth {
namespace {

constexpr char TAG[] = "auth";
constexpr char kNs[] = "deck32";
constexpr uint32_t kIterations = 20000;
constexpr int kMaxSessions = 8;
constexpr int64_t kIdleUs = 7LL * 24 * 3600 * 1000000;  // a week without use
constexpr int kFreeTries = 5;
constexpr int64_t kLockUs = 30LL * 1000000;

using Digest = std::array<uint8_t, 32>;

std::mutex g_mutex;
bool g_set = false;
std::array<uint8_t, 16> g_salt{};
Digest g_hash{};

struct Session {
    std::string token;
    int64_t used = 0;
};
Session g_sessions[kMaxSessions];
int g_failures = 0;
int64_t g_locked_until = 0;

Digest sha256(const uint8_t* a, size_t an, const uint8_t* b = nullptr, size_t bn = 0) {
    Digest out{};
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    size_t len = 0;
    psa_hash_setup(&op, PSA_ALG_SHA_256);
    psa_hash_update(&op, a, an);
    if (b && bn) psa_hash_update(&op, b, bn);
    psa_hash_finish(&op, out.data(), out.size(), &len);
    return out;
}

// HMAC-SHA256 with the key already padded into ipad/opad blocks.
struct Hmac {
    uint8_t ipad[64], opad[64];
    explicit Hmac(const std::string& key) {
        uint8_t k[64] = {};
        if (key.size() > 64) {
            const Digest d = sha256(reinterpret_cast<const uint8_t*>(key.data()), key.size());
            std::memcpy(k, d.data(), d.size());
        } else {
            std::memcpy(k, key.data(), key.size());
        }
        for (int i = 0; i < 64; ++i) {
            ipad[i] = k[i] ^ 0x36;
            opad[i] = k[i] ^ 0x5c;
        }
    }
    Digest operator()(const uint8_t* msg, size_t n) const {
        const Digest inner = sha256(ipad, sizeof(ipad), msg, n);
        return sha256(opad, sizeof(opad), inner.data(), inner.size());
    }
};

// PBKDF2-HMAC-SHA256, one 32-byte block.
Digest pbkdf2(const std::string& password, const std::array<uint8_t, 16>& salt) {
    const Hmac hmac(password);
    uint8_t s[20];
    std::memcpy(s, salt.data(), 16);
    s[16] = 0;
    s[17] = 0;
    s[18] = 0;
    s[19] = 1;
    Digest u = hmac(s, sizeof(s));
    Digest t = u;
    for (uint32_t i = 1; i < kIterations; ++i) {
        u = hmac(u.data(), u.size());
        for (size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
    }
    return t;
}

bool equal(const Digest& a, const Digest& b) {
    uint8_t d = 0;
    for (size_t i = 0; i < a.size(); ++i) d |= a[i] ^ b[i];
    return d == 0;
}

std::string random_token() {
    uint8_t b[16];
    esp_fill_random(b, sizeof(b));
    static const char* kHex = "0123456789abcdef";
    std::string t;
    for (uint8_t x : b) {
        t += kHex[x >> 4];
        t += kHex[x & 15];
    }
    return t;
}

}  // namespace

void load() {
    psa_crypto_init();
    std::lock_guard<std::mutex> lock(g_mutex);
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return;
    size_t sl = g_salt.size(), hl = g_hash.size();
    g_set = nvs_get_blob(h, "pw_salt", g_salt.data(), &sl) == ESP_OK && sl == g_salt.size() &&
            nvs_get_blob(h, "pw_hash", g_hash.data(), &hl) == ESP_OK && hl == g_hash.size();
    nvs_close(h);
    ESP_LOGI(TAG, "web password %s", g_set ? "set" : "not set yet");
}

bool password_set() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_set;
}

bool set_password(const std::string& password) {
    if (password.size() < kMinLength) return false;
    std::array<uint8_t, 16> salt;
    esp_fill_random(salt.data(), salt.size());
    const Digest hash = pbkdf2(password, salt);
    std::lock_guard<std::mutex> lock(g_mutex);
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
    const bool ok = nvs_set_blob(h, "pw_salt", salt.data(), salt.size()) == ESP_OK &&
                    nvs_set_blob(h, "pw_hash", hash.data(), hash.size()) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (!ok) return false;
    g_salt = salt;
    g_hash = hash;
    g_set = true;
    for (auto& s : g_sessions) s = Session{};  // a new password signs everyone out
    ESP_LOGI(TAG, "web password changed");
    return true;
}

bool check_password(const std::string& password) {
    std::array<uint8_t, 16> salt;
    Digest want;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_set) return false;
        salt = g_salt;
        want = g_hash;
    }
    return equal(pbkdf2(password, salt), want);
}

bool sign_in_locked() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return esp_timer_get_time() < g_locked_until;
}

void sign_in_failed() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (++g_failures >= kFreeTries) {
        g_locked_until = esp_timer_get_time() + kLockUs;
        g_failures = 0;
        ESP_LOGW(TAG, "too many failed sign-ins: sign-in locked for 30 s");
    }
}

void sign_in_ok() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_failures = 0;
}

Login login(const std::string& password, std::string& token) {
    if (sign_in_locked()) return Login::Locked;
    if (!check_password(password)) {
        sign_in_failed();
        return Login::Wrong;
    }
    sign_in_ok();
    token = new_session();
    return Login::Ok;
}

std::string new_session() {
    std::lock_guard<std::mutex> lock(g_mutex);
    const int64_t now = esp_timer_get_time();
    Session* slot = &g_sessions[0];
    for (auto& s : g_sessions) {
        if (s.token.empty() || now - s.used > kIdleUs) {
            slot = &s;
            break;
        }
        if (s.used < slot->used) slot = &s;  // otherwise the least recently used
    }
    slot->token = random_token();
    slot->used = now;
    return slot->token;
}

bool valid(const std::string& token) {
    if (token.size() != 32) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    const int64_t now = esp_timer_get_time();
    for (auto& s : g_sessions) {
        if (s.token.empty() || now - s.used > kIdleUs) continue;
        // Constant-time compare of equal-length strings.
        uint8_t d = 0;
        for (size_t i = 0; i < 32; ++i) d |= static_cast<uint8_t>(s.token[i] ^ token[i]);
        if (d == 0) {
            s.used = now;
            return true;
        }
    }
    return false;
}

void end(const std::string& token) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& s : g_sessions)
        if (s.token == token) s = Session{};
}

void end_all() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& s : g_sessions) s = Session{};
}

}  // namespace auth
