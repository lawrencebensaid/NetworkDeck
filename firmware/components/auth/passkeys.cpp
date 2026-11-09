// Passkeys on the dock: the stored list, the challenges handed out, and the PSA crypto the WebAuthn
// checks (webauthn.cpp) use. See auth.hpp.

#include <cstring>
#include <mutex>

#include "auth.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "webauthn.hpp"

namespace auth {
namespace {

constexpr char TAG[] = "passkey";
constexpr char kNs[] = "deck32";
constexpr char kKey[] = "passkeys";
constexpr int64_t kChallengeUs = 120LL * 1000000;
constexpr int kLoginChallenges = 4;  // a few pages may be signing in at once
// The one account: its WebAuthn user handle.
constexpr char kUserHandle[] = "networkdeck-admin";

struct Challenge {
    std::string value;  // base64url
    int64_t until = 0;
};

std::mutex g_mutex;
Challenge g_create;
Challenge g_get[kLoginChallenges];
int g_get_next = 0;

std::string new_challenge() {
    uint8_t b[32];
    esp_fill_random(b, sizeof(b));
    return webauthn::b64url(b, sizeof(b));
}

webauthn::Hash sha256(const uint8_t* d, size_t n) {
    webauthn::Hash out{};
    size_t len = 0;
    psa_hash_compute(PSA_ALG_SHA_256, d, n, out.data(), out.size(), &len);
    return out;
}

// DER SEQUENCE { INTEGER r, INTEGER s } -> r || s, 32 bytes each, as PSA wants it.
bool der_to_raw(const webauthn::Bytes& der, uint8_t raw[64]) {
    size_t i = 0;
    auto byte = [&](uint8_t& b) { return i < der.size() ? (b = der[i++], true) : false; };
    uint8_t t, len;
    if (!byte(t) || t != 0x30 || !byte(len) || len & 0x80) return false;
    for (int part = 0; part < 2; ++part) {
        uint8_t il;
        if (!byte(t) || t != 0x02 || !byte(il) || il == 0 || il > 33 || i + il > der.size()) return false;
        const uint8_t* v = &der[i];
        i += il;
        while (il > 32 && *v == 0) ++v, --il;  // the sign byte
        if (il > 32) return false;
        std::memset(raw + part * 32, 0, 32);
        std::memcpy(raw + part * 32 + (32 - il), v, il);
    }
    return true;
}

bool verify(const webauthn::Credential& key, const webauthn::Hash& hash, const webauthn::Bytes& der_sig) {
    uint8_t raw[64];
    if (!der_to_raw(der_sig, raw)) return false;
    uint8_t point[65];
    point[0] = 0x04;
    std::memcpy(point + 1, key.x.data(), 32);
    std::memcpy(point + 33, key.y.data(), 32);
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t id = 0;
    if (psa_import_key(&attr, point, sizeof(point), &id) != PSA_SUCCESS) return false;
    const bool ok = psa_verify_hash(id, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash.data(), hash.size(), raw, sizeof(raw)) == PSA_SUCCESS;
    psa_destroy_key(id);
    return ok;
}

webauthn::Crypto crypto() {
    webauthn::Crypto c;
    c.sha256 = sha256;
    c.verify = verify;
    return c;
}

std::vector<webauthn::Credential> load_all() {
    std::vector<webauthn::Credential> out;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return out;
    size_t len = 0;
    if (nvs_get_blob(h, kKey, nullptr, &len) == ESP_OK && len > 0) {
        webauthn::Bytes blob(len);
        if (nvs_get_blob(h, kKey, blob.data(), &len) == ESP_OK) webauthn::unpack(blob, out);
    }
    nvs_close(h);
    return out;
}

bool save_all(const std::vector<webauthn::Credential>& all) {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
    const webauthn::Bytes blob = webauthn::pack(all);
    const bool ok = nvs_set_blob(h, kKey, blob.data(), blob.size()) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

std::string json_str(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) o += c;
    }
    return o + "\"";
}

std::string credential_list(const std::vector<webauthn::Credential>& all) {
    std::string j = "[";
    for (size_t i = 0; i < all.size(); ++i)
        j += std::string(i ? "," : "") + "{\"type\":\"public-key\",\"id\":\"" + webauthn::b64url(all[i].id) + "\"}";
    return j + "]";
}

}  // namespace

std::vector<Passkey> passkeys() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<Passkey> out;
    for (const auto& c : load_all()) out.push_back({webauthn::b64url(c.id), c.name});
    return out;
}

std::string passkey_create_options(const std::string& rp_id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_create = {new_challenge(), esp_timer_get_time() + kChallengeUs};
    const std::string user = webauthn::b64url(reinterpret_cast<const uint8_t*>(kUserHandle), sizeof(kUserHandle) - 1);
    return "{\"challenge\":\"" + g_create.value + "\",\"rp\":{\"id\":" + json_str(rp_id) + ",\"name\":\"NetworkDeck\"}" +
           ",\"user\":{\"id\":\"" + user + "\",\"name\":\"admin\",\"displayName\":\"NetworkDeck administrator\"}" +
           ",\"pubKeyCredParams\":[{\"type\":\"public-key\",\"alg\":-7}],\"timeout\":120000,\"attestation\":\"none\"" +
           ",\"authenticatorSelection\":{\"residentKey\":\"preferred\",\"userVerification\":\"preferred\"}" +
           ",\"excludeCredentials\":" + credential_list(load_all()) + "}";
}

const char* passkey_add(const std::string& rp_id, const std::string& origin, const std::string& client_data_b64,
                        const std::string& attestation_b64, const std::string& name) {
    webauthn::Bytes client, attestation;
    if (!webauthn::from_b64url(client_data_b64, client) || !webauthn::from_b64url(attestation_b64, attestation))
        return "The browser's answer is not readable";
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_create.value.empty() || esp_timer_get_time() > g_create.until) return "The request expired: try again";
    const webauthn::Expect expect{g_create.value, origin, rp_id};
    g_create = {};  // one answer per challenge
    webauthn::Credential cred;
    if (const char* why = webauthn::check_registration(client, attestation, expect, crypto(), cred)) return why;
    auto all = load_all();
    for (const auto& c : all)
        if (c.id == cred.id) return "This passkey is added already";
    if (all.size() >= kMaxPasskeys) return "There are 8 passkeys already: remove one first";
    cred.name = name.empty() ? "Passkey" : name;
    all.push_back(cred);
    if (!save_all(all)) return "Could not store the passkey";
    ESP_LOGI(TAG, "passkey added (%u in all)", static_cast<unsigned>(all.size()));
    return nullptr;
}

bool passkey_remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto all = load_all();
    const size_t before = all.size();
    for (auto it = all.begin(); it != all.end();)
        it = webauthn::b64url(it->id) == id ? all.erase(it) : it + 1;
    return all.size() != before && save_all(all);
}

std::string passkey_get_options(const std::string& rp_id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Challenge& c = g_get[g_get_next++ % kLoginChallenges];
    c = {new_challenge(), esp_timer_get_time() + kChallengeUs};
    return "{\"challenge\":\"" + c.value + "\",\"rpId\":" + json_str(rp_id) +
           ",\"timeout\":120000,\"userVerification\":\"preferred\",\"allowCredentials\":" + credential_list(load_all()) + "}";
}

Login passkey_login(const std::string& rp_id, const std::string& origin, const std::string& id_b64,
                    const std::string& client_data_b64, const std::string& authenticator_data_b64,
                    const std::string& signature_b64, std::string& token, const char*& why) {
    why = nullptr;
    if (sign_in_locked()) return Login::Locked;
    webauthn::Bytes client, authenticator, signature, id;
    if (!webauthn::from_b64url(client_data_b64, client) || !webauthn::from_b64url(authenticator_data_b64, authenticator) ||
        !webauthn::from_b64url(signature_b64, signature) || !webauthn::from_b64url(id_b64, id)) {
        why = "The browser's answer is not readable";
        return Login::Wrong;
    }
    std::string type, challenge, client_origin;
    webauthn::client_data(std::string(client.begin(), client.end()), type, challenge, client_origin);

    std::lock_guard<std::mutex> lock(g_mutex);
    // The challenge must be one handed out and not used yet; it is used up now either way.
    bool handed_out = false;
    for (auto& c : g_get) {
        if (!c.value.empty() && c.value == challenge && esp_timer_get_time() <= c.until) handed_out = true;
        if (c.value == challenge) c = {};
    }
    if (!handed_out) {
        why = "The request expired: try again";
        return Login::Wrong;
    }
    auto all = load_all();
    for (auto& cred : all) {
        if (cred.id != id) continue;
        uint32_t count = 0;
        why = webauthn::check_assertion(cred, client, authenticator, signature, {challenge, origin, rp_id}, crypto(), count);
        if (why) {
            sign_in_failed();
            return Login::Wrong;
        }
        if (count != cred.sign_count) {
            cred.sign_count = count;
            save_all(all);
        }
        sign_in_ok();
        token = new_session();
        ESP_LOGI(TAG, "signed in with passkey \"%s\"", cred.name.c_str());
        return Login::Ok;
    }
    sign_in_failed();
    why = "This passkey is not on the dock (any more)";
    return Login::Wrong;
}

}  // namespace auth
