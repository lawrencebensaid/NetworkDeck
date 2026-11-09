// Passkeys: the WebAuthn checks, with stand-in crypto (the parsing and the rules are what is tested
// here; the dock's PSA hash and signature check are exercised on the hardware with a real passkey).
#include <doctest/doctest.h>

#include <string>

#include "webauthn.hpp"

using namespace webauthn;

namespace {

// Not SHA-256: a stand-in that is deterministic and depends on every byte.
Hash fake_hash(const uint8_t* d, size_t n) {
    Hash h{};
    uint32_t a = 2166136261u;
    for (size_t i = 0; i < n; ++i) a = (a ^ d[i]) * 16777619u;
    for (size_t i = 0; i < h.size(); ++i) {
        a = (a ^ static_cast<uint8_t>(i)) * 16777619u;
        h[i] = static_cast<uint8_t>(a >> 24);
    }
    return h;
}

Hash last_signed;
Crypto crypto() {
    Crypto c;
    c.sha256 = fake_hash;
    // "Valid" when the signature is the 32-byte hash it signs.
    c.verify = [](const Credential&, const Hash& h, const Bytes& sig) {
        last_signed = h;
        return sig == Bytes(h.begin(), h.end());
    };
    return c;
}

Bytes str(const std::string& s) { return Bytes(s.begin(), s.end()); }

const Expect kExpect{"Y2hhbGxlbmdl", "https://deck32-204c.local", "deck32-204c.local"};

Bytes client(const char* type, const std::string& challenge = kExpect.challenge, const std::string& origin = kExpect.origin) {
    return str(std::string("{\"type\":\"") + type + "\",\"challenge\":\"" + challenge + "\",\"origin\":\"" + origin +
               "\",\"crossOrigin\":false,\"other_keys_can_be_added_here\":\"do not compare clientDataJSON against a template\"}");
}

void cbor_head(Bytes& b, int major, uint64_t v) {
    if (v < 24) b.push_back(static_cast<uint8_t>(major << 5 | v));
    else if (v < 256) { b.push_back(static_cast<uint8_t>(major << 5 | 24)); b.push_back(static_cast<uint8_t>(v)); }
    else { b.push_back(static_cast<uint8_t>(major << 5 | 25)); b.push_back(static_cast<uint8_t>(v >> 8)); b.push_back(static_cast<uint8_t>(v)); }
}
void cbor_int(Bytes& b, int64_t v) { v >= 0 ? cbor_head(b, 0, static_cast<uint64_t>(v)) : cbor_head(b, 1, static_cast<uint64_t>(-1 - v)); }
void cbor_bytes(Bytes& b, const Bytes& d) { cbor_head(b, 2, d.size()); b.insert(b.end(), d.begin(), d.end()); }
void cbor_text(Bytes& b, const std::string& t) { cbor_head(b, 3, t.size()); b.insert(b.end(), t.begin(), t.end()); }

Bytes auth_data(uint8_t flags, uint32_t count, const Bytes& cred_id = {}, int64_t alg = -7) {
    const Hash rp = fake_hash(reinterpret_cast<const uint8_t*>(kExpect.rp_id.data()), kExpect.rp_id.size());
    Bytes a(rp.begin(), rp.end());
    a.push_back(flags);
    for (int s = 24; s >= 0; s -= 8) a.push_back(static_cast<uint8_t>(count >> s));
    if (flags & 0x40) {
        a.insert(a.end(), 16, 0xaa);  // aaguid
        a.push_back(static_cast<uint8_t>(cred_id.size() >> 8));
        a.push_back(static_cast<uint8_t>(cred_id.size()));
        a.insert(a.end(), cred_id.begin(), cred_id.end());
        cbor_head(a, 5, 5);  // COSE key, with an unknown key thrown in
        cbor_int(a, 1); cbor_int(a, 2);
        cbor_int(a, 3); cbor_int(a, alg);
        cbor_int(a, -1); cbor_int(a, 1);
        cbor_int(a, -2); cbor_bytes(a, Bytes(32, 0x11));
        cbor_int(a, -3); cbor_bytes(a, Bytes(32, 0x22));
    }
    return a;
}

Bytes attestation(const Bytes& auth) {
    Bytes b;
    cbor_head(b, 5, 3);
    cbor_text(b, "fmt"); cbor_text(b, "none");
    cbor_text(b, "attStmt"); cbor_head(b, 5, 0);
    cbor_text(b, "authData"); cbor_bytes(b, auth);
    return b;
}

}  // namespace

TEST_CASE("base64url round-trips and takes padding and standard base64") {
    for (size_t n = 0; n < 8; ++n) {
        Bytes b;
        for (size_t i = 0; i < n; ++i) b.push_back(static_cast<uint8_t>(0xf0 + i * 7));
        Bytes back;
        REQUIRE(from_b64url(b64url(b), back));
        CHECK(back == b);
    }
    CHECK(b64url(str("hi?")) == "aGk_");
    Bytes out;
    CHECK(from_b64url("aGk/", out));
    CHECK(out == str("hi?"));
    CHECK(from_b64url("aGk=", out));
    CHECK(out == str("hi"));
    CHECK_FALSE(from_b64url("a", out));
    CHECK_FALSE(from_b64url("a*b", out));
}

TEST_CASE("client data: the three fields, whatever else and whatever order") {
    std::string t, c, o;
    CHECK(client_data(" { \"origin\" : \"https://x\", \"tokenBinding\":{\"status\":\"present\",\"id\":[1,2]}, \"type\":\"webauthn.get\",\"challenge\":\"ab\\u002dc\"}", t, c, o));
    CHECK(t == "webauthn.get");
    CHECK(c == "ab-c");
    CHECK(o == "https://x");
    CHECK_FALSE(client_data("{\"type\":\"webauthn.get\",\"challenge\":\"x\"}", t, c, o));
    CHECK_FALSE(client_data("[1]", t, c, o));
    CHECK_FALSE(client_data("{\"type\":\"x\",\"challenge\":\"y\",\"origin\":\"z\"", t, c, o));
}

TEST_CASE("registration: a passkey made for this dock gives its id and key") {
    const Bytes id{1, 2, 3, 4, 5};
    Credential cred;
    REQUIRE(check_registration(client("webauthn.create"), attestation(auth_data(0x45, 0, id)), kExpect, crypto(), cred) == nullptr);
    CHECK(cred.id == id);
    CHECK(cred.x[0] == 0x11);
    CHECK(cred.y[31] == 0x22);
}

TEST_CASE("registration: refused for the wrong ceremony, challenge, origin, address, presence or key type") {
    const Bytes id{9};
    Credential cred;
    const Bytes good = attestation(auth_data(0x45, 0, id));
    CHECK(check_registration(client("webauthn.get"), good, kExpect, crypto(), cred) != nullptr);
    CHECK(check_registration(client("webauthn.create", "other"), good, kExpect, crypto(), cred) != nullptr);
    CHECK(check_registration(client("webauthn.create", kExpect.challenge, "http://deck32-204c.local"), good, kExpect, crypto(), cred) != nullptr);
    Expect elsewhere = kExpect;
    elsewhere.rp_id = "evil.example";
    elsewhere.origin = kExpect.origin;
    CHECK(check_registration(client("webauthn.create"), good, elsewhere, crypto(), cred) != nullptr);
    CHECK(check_registration(client("webauthn.create"), attestation(auth_data(0x44, 0, id)), kExpect, crypto(), cred) != nullptr);  // no UP
    CHECK(check_registration(client("webauthn.create"), attestation(auth_data(0x45, 0, id, -257)), kExpect, crypto(), cred) != nullptr);  // RS256
    CHECK(check_registration(client("webauthn.create"), Bytes{0xa1}, kExpect, crypto(), cred) != nullptr);  // truncated
}

TEST_CASE("assertion: signed over authenticator data and the client data's hash; the counter must rise") {
    Credential cred;
    cred.id = {7};
    const Bytes cd = client("webauthn.get");
    const Bytes ad = auth_data(0x05, 10);
    const Hash ch = fake_hash(cd.data(), cd.size());
    Bytes signed_data(ad);
    signed_data.insert(signed_data.end(), ch.begin(), ch.end());
    const Hash want = fake_hash(signed_data.data(), signed_data.size());
    const Bytes sig(want.begin(), want.end());

    uint32_t count = 0;
    CHECK(check_assertion(cred, cd, ad, sig, kExpect, crypto(), count) == nullptr);
    CHECK(count == 10);
    CHECK(last_signed == want);

    CHECK(check_assertion(cred, cd, ad, Bytes(32, 0), kExpect, crypto(), count) != nullptr);  // bad signature
    cred.sign_count = 10;
    CHECK(check_assertion(cred, cd, ad, sig, kExpect, crypto(), count) != nullptr);  // counter did not rise
    cred.sign_count = 0;
    CHECK(check_assertion(cred, client("webauthn.create"), ad, sig, kExpect, crypto(), count) != nullptr);
    CHECK(check_assertion(cred, cd, auth_data(0x04, 11), sig, kExpect, crypto(), count) != nullptr);  // no UP
}

TEST_CASE("assertion: authenticators that do not count (0 and 0) pass") {
    Credential cred;
    const Bytes cd = client("webauthn.get");
    const Bytes ad = auth_data(0x01, 0);
    const Hash ch = fake_hash(cd.data(), cd.size());
    Bytes signed_data(ad);
    signed_data.insert(signed_data.end(), ch.begin(), ch.end());
    const Hash want = fake_hash(signed_data.data(), signed_data.size());
    uint32_t count = 99;
    CHECK(check_assertion(cred, cd, ad, Bytes(want.begin(), want.end()), kExpect, crypto(), count) == nullptr);
    CHECK(count == 0);
}

TEST_CASE("the stored list packs and unpacks") {
    Credential a;
    a.id = {1, 2, 3};
    a.x.fill(5);
    a.y.fill(6);
    a.sign_count = 0x01020304;
    a.name = "MacBook";
    Credential b;
    b.id = Bytes(64, 9);
    b.name = std::string(80, 'n');  // stored cut to 64
    std::vector<Credential> back;
    REQUIRE(unpack(pack({a, b}), back));
    REQUIRE(back.size() == 2);
    CHECK(back[0].id == a.id);
    CHECK(back[0].x == a.x);
    CHECK(back[0].sign_count == a.sign_count);
    CHECK(back[0].name == "MacBook");
    CHECK(back[1].name.size() == 64);
    CHECK_FALSE(unpack(Bytes{2, 0}, back));
    CHECK_FALSE(unpack(Bytes{1, 1, 5}, back));
}
