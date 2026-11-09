// The web interface's password and sign-in sessions.
//
// The password is stored as a salted PBKDF2-HMAC-SHA256 hash in NVS (namespace "deck32", so a factory
// reset erases it). Sessions are random 128-bit tokens kept in RAM: a reboot signs everyone out.
#pragma once

#include <cstdint>
#include <string>

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

inline constexpr size_t kMinLength = 8;

}  // namespace auth
