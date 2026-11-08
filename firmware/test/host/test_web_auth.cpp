#include <doctest/doctest.h>
#include "auth_policy.hpp"

using namespace web::auth;

TEST_CASE("web password bounds reject empty, short, oversized and embedded NUL") {
    CHECK_FALSE(password_valid(""));
    CHECK_FALSE(password_valid(std::string(11, 'a')));
    CHECK(password_valid(std::string(12, 'a')));
    CHECK(password_valid(std::string(128, 'a')));
    CHECK_FALSE(password_valid(std::string(129, 'a')));
    CHECK_FALSE(password_valid(std::string("123456789012\0more", 17)));
}

TEST_CASE("session cookie parser requires exact name and fixed lowercase hex token") {
    const std::string token(64, 'a');
    CHECK(session_cookie("nd_session=" + token) == token);
    CHECK(session_cookie("other=x; \tnd_session=" + token + "; third=y") == token);
    CHECK(session_cookie("not_nd_session=" + token).empty());
    CHECK(session_cookie("other=nd_session=" + token).empty());
    CHECK(session_cookie("nd_session=" + std::string(63, 'a')).empty());
    CHECK(session_cookie("nd_session=" + std::string(65, 'a')).empty());
    CHECK(session_cookie("nd_session=" + std::string(64, 'A')).empty());
    CHECK(session_cookie("nd_session=" + std::string(64, 'z')).empty());
    CHECK(session_cookie("nd_session=; nd_session=" + token).empty());
    CHECK(session_cookie("").empty());
}

TEST_CASE("session has absolute expiry, rejects absent/stale/wrong tokens and reboot state") {
    const std::string token(64, 'a');
    const std::string cookie = "nd_session=" + token;
    CHECK(session_valid(token, 100, cookie, 99));
    CHECK_FALSE(session_valid(token, 100, cookie, 100));
    CHECK_FALSE(session_valid(token, 100, cookie, 101));
    CHECK_FALSE(session_valid(token, 100, "", 99));
    CHECK_FALSE(session_valid(std::string(64, 'b'), 100, cookie, 99));
    CHECK_FALSE(session_valid("", 100, cookie, 99));
    CHECK_FALSE(session_valid(token, 0, cookie, 0));
    CHECK_FALSE(session_valid(std::string(63, 'a'), 100, cookie, 99));
}

TEST_CASE("constant comparison covers binary credential hashes as well as tokens") {
    CHECK(constant_equal("", ""));
    CHECK(constant_equal("abc", "abc"));
    CHECK_FALSE(constant_equal("abc", "abd"));
    CHECK_FALSE(constant_equal("abc", "xbc"));
    CHECK_FALSE(constant_equal("abc", "abcd"));
    CHECK(constant_equal(std::string(32, '\0'), std::string(32, '\0')));
    CHECK_FALSE(constant_equal(std::string(32, '\0'), std::string(32, 'a')));
}
