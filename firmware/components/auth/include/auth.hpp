// The web interface's password and sign-in sessions.
//
// The password is stored as a salted PBKDF2-HMAC-SHA256 hash in NVS (namespace "deck32", so a factory
// reset erases it). Sessions are random 128-bit tokens kept in RAM: a reboot signs everyone out.
// Passkeys are a second way to sign in, next to the password.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace auth {

void load();
bool password_set();
// Returns false if the password is too short (8+ characters).
bool set_password(const std::string& password);
bool check_password(const std::string& password);

enum class Login { Ok, Wrong, Locked };
// Checks the password with a lockout after repeated failures; on Ok, `token` is a new session.
Login login(const std::string& password, std::string& token);
std::string new_session();
bool valid(const std::string& token);  // also refreshes its idle timer
void end(const std::string& token);
void end_all();

// The lockout both sign-ins share: five failures lock sign-in for 30 s.
bool sign_in_locked();
void sign_in_failed();
void sign_in_ok();

// ── passkeys (WebAuthn, see webauthn.hpp) ──
// Stored in NVS (namespace "deck32", so a factory reset erases them), at most kMaxPasskeys.
// `rp_id` is the hostname the page was opened at ("deck32-204c.local"); `origin` is "https://" + it.

struct Passkey {
    std::string id;  // base64url
    std::string name;
};
inline constexpr size_t kMaxPasskeys = 8;
std::vector<Passkey> passkeys();

// navigator.credentials.create() options as JSON. The challenge is good for one answer, 2 minutes.
std::string passkey_create_options(const std::string& rp_id);
// Checks the browser's answer and stores the passkey. nullptr on success, otherwise why not.
const char* passkey_add(const std::string& rp_id, const std::string& origin, const std::string& client_data_b64,
                        const std::string& attestation_b64, const std::string& name);
bool passkey_remove(const std::string& id);

// navigator.credentials.get() options as JSON (any of the stored passkeys).
std::string passkey_get_options(const std::string& rp_id);
// Sign-in with a passkey: on Ok, `token` is a new session; on Wrong, `why` says why.
Login passkey_login(const std::string& rp_id, const std::string& origin, const std::string& id_b64,
                    const std::string& client_data_b64, const std::string& authenticator_data_b64,
                    const std::string& signature_b64, std::string& token, const char*& why);

// ── API keys ──
// For scripts and tools: sent as "Authorization: Bearer ndk_…". Stored as SHA-256 hashes in NVS
// (namespace "deck32", so a factory reset erases them); the key itself is shown once, when it is made.
struct ApiKey {
    std::string id;     // 12 hex characters
    std::string name;
    uint32_t created;   // Unix time, 0 if the clock wasn't set
    uint32_t last_used; // Unix time since this start, 0 if not used (api_key_check: 0 on the first use)
};
inline constexpr size_t kMaxApiKeys = 8;
std::vector<ApiKey> api_keys();
// The new key (shown once), or empty if the name is empty or too long, or there are kMaxApiKeys.
std::string api_key_create(const std::string& name, std::string& id);
bool api_key_remove(const std::string& id);
// True if `secret` is a stored key; `who` is it.
bool api_key_check(const std::string& secret, ApiKey& who);

inline constexpr size_t kMinLength = 8;

}  // namespace auth
