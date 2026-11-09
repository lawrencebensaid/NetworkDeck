// Passkeys (WebAuthn), the checks only: what a browser sends when a passkey is made or used, parsed
// and checked against what the dock asked for. No IDF headers: the host tests run it. The two crypto
// operations come from the caller (PSA on the dock, stand-ins in the tests).
//
// Only ES256 (ECDSA P-256 with SHA-256) keys, the one every passkey provider makes. Attestation is not
// checked (the dock asks for none): a passkey is trusted because a signed-in user added it.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace webauthn {

using Bytes = std::vector<uint8_t>;
using Hash = std::array<uint8_t, 32>;

std::string b64url(const uint8_t* data, size_t len);
inline std::string b64url(const Bytes& b) { return b64url(b.data(), b.size()); }
// Accepts base64url with or without padding (and standard base64's + and /).
bool from_b64url(const std::string& s, Bytes& out);

struct Credential {
    Bytes id;
    std::array<uint8_t, 32> x{}, y{};  // the P-256 public key
    uint32_t sign_count = 0;
    std::string name;
};

struct Crypto {
    std::function<Hash(const uint8_t* data, size_t len)> sha256;
    // ECDSA P-256 over a SHA-256 `hash`, the signature DER-encoded (as WebAuthn sends it).
    std::function<bool(const Credential& key, const Hash& hash, const Bytes& der_signature)> verify;
};

// What this ceremony must match: the challenge the dock handed out (base64url), the page's origin
// ("https://deck32-204c.local") and the relying party id (the hostname, "deck32-204c.local").
struct Expect {
    std::string challenge;
    std::string origin;
    std::string rp_id;
};

// The fields of clientDataJSON the checks use. False if it is not a JSON object with them.
bool client_data(const std::string& json, std::string& type, std::string& challenge, std::string& origin);

// navigator.credentials.create(): checks clientDataJSON and the attestation object, and returns the
// new credential (id and key) in `out`. nullptr when it passes, otherwise why not.
const char* check_registration(const Bytes& client_data_json, const Bytes& attestation_object, const Expect& expect,
                               const Crypto& crypto, Credential& out);

// navigator.credentials.get(): checks a signature by `cred`. On success `new_count` is the
// authenticator's signature counter (0 for the many that do not count).
const char* check_assertion(const Credential& cred, const Bytes& client_data_json, const Bytes& authenticator_data,
                            const Bytes& signature, const Expect& expect, const Crypto& crypto, uint32_t& new_count);

// The stored list (NVS blob).
Bytes pack(const std::vector<Credential>& creds);
bool unpack(const Bytes& blob, std::vector<Credential>& out);

}  // namespace webauthn
