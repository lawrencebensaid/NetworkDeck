#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace web::auth {
constexpr size_t kTokenLength = 64;
constexpr int64_t kLifetimeUs = 30LL * 60 * 1000000;
constexpr int64_t kRetryUs = 1000000;

inline bool password_valid(const std::string& password) {
    return password.size() >= 12 && password.size() <= 128 && password.find('\0') == std::string::npos;
}

inline bool constant_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

// Match a whole cookie, never a substring or another cookie's value.
inline std::string session_cookie(const std::string& cookies) {
    size_t start = 0;
    while (start < cookies.size()) {
        const auto end = cookies.find(';', start);
        auto part = cookies.substr(start, end == std::string::npos ? end : end - start);
        const auto first = part.find_first_not_of(" \t");
        if (first != std::string::npos) part.erase(0, first);
        const std::string prefix = "nd_session=";
        if (part.compare(0, prefix.size(), prefix) == 0) {
            auto value = part.substr(prefix.size());
            if (value.size() != kTokenLength) return {};
            for (char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return {};
            return value;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}

inline bool session_valid(const std::string& expected, int64_t expires, const std::string& cookies, int64_t now) {
    return expected.size() == kTokenLength && now < expires && constant_equal(expected, session_cookie(cookies));
}
}  // namespace web::auth
