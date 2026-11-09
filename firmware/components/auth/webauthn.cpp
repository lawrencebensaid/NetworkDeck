#include "webauthn.hpp"

#include <cstring>

namespace webauthn {
namespace {

constexpr uint8_t kUserPresent = 0x01;
constexpr uint8_t kAttestedData = 0x40;

// ── a minimal CBOR reader: what attestation objects and COSE keys use ──

struct Cbor {
    const uint8_t* p;
    const uint8_t* end;

    // The head of the next item: major type and its argument. False at the end or on an encoding
    // this reader does not take (indefinite lengths, which WebAuthn does not use).
    bool head(int& major, uint64_t& arg) {
        if (p >= end) return false;
        const uint8_t b = *p++;
        major = b >> 5;
        const uint8_t info = b & 0x1f;
        if (info < 24) {
            arg = info;
            return true;
        }
        const int n = info == 24 ? 1 : info == 25 ? 2 : info == 26 ? 4 : info == 27 ? 8 : 0;
        if (n == 0 || end - p < n) return false;
        arg = 0;
        for (int i = 0; i < n; ++i) arg = arg << 8 | *p++;
        return true;
    }

    bool skip(int depth = 0) {
        if (depth > 8) return false;
        int major;
        uint64_t arg;
        if (!head(major, arg)) return false;
        switch (major) {
            case 0:
            case 1:
            case 7:
                return true;
            case 2:
            case 3:
                if (static_cast<uint64_t>(end - p) < arg) return false;
                p += arg;
                return true;
            case 4:
                for (uint64_t i = 0; i < arg; ++i)
                    if (!skip(depth + 1)) return false;
                return true;
            case 5:
                for (uint64_t i = 0; i < arg * 2; ++i)
                    if (!skip(depth + 1)) return false;
                return true;
            case 6:
                return skip(depth + 1);
        }
        return false;
    }

    bool bytes(int want_major, const uint8_t*& data, size_t& len) {
        int major;
        uint64_t arg;
        if (!head(major, arg) || major != want_major || static_cast<uint64_t>(end - p) < arg) return false;
        data = p;
        len = static_cast<size_t>(arg);
        p += arg;
        return true;
    }

    bool integer(int64_t& v) {
        int major;
        uint64_t arg;
        if (!head(major, arg) || (major != 0 && major != 1) || arg > INT64_MAX) return false;
        v = major == 0 ? static_cast<int64_t>(arg) : -1 - static_cast<int64_t>(arg);
        return true;
    }
};

// ── a minimal JSON reader for clientDataJSON's top-level strings ──

struct Json {
    const char* p;
    const char* end;

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool str(std::string& out) {
        ws();
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\') {
                if (p >= end) return false;
                const char e = *p++;
                switch (e) {
                    case '"': case '\\': case '/': c = e; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'u': {
                        if (end - p < 4) return false;
                        unsigned v = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = *p++;
                            v = v * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 99);
                            if (v > 0xffff) return false;
                        }
                        if (v >= 0x80) return false;  // the fields checked are ASCII
                        c = static_cast<char>(v);
                        break;
                    }
                    default: return false;
                }
            }
            out += c;
        }
        if (p >= end) return false;
        ++p;
        return true;
    }
    bool value(int depth = 0) {  // skip any value
        ws();
        if (p >= end || depth > 8) return false;
        if (*p == '"') {
            std::string s;
            return str(s);
        }
        if (*p == '{' || *p == '[') {
            const char close = *p == '{' ? '}' : ']';
            const bool object = *p == '{';
            ++p;
            ws();
            if (p < end && *p == close) return ++p, true;
            for (;;) {
                if (object) {
                    std::string k;
                    if (!str(k)) return false;
                    ws();
                    if (p >= end || *p++ != ':') return false;
                }
                if (!value(depth + 1)) return false;
                ws();
                if (p >= end) return false;
                if (*p == ',') { ++p; continue; }
                if (*p == close) return ++p, true;
                return false;
            }
        }
        while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') ++p;
        return true;
    }
};

uint32_t be32(const uint8_t* b) { return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]; }

bool same(const Hash& a, const uint8_t* b) { return std::memcmp(a.data(), b, a.size()) == 0; }

// The checks both ceremonies make on clientDataJSON.
const char* check_client(const Bytes& raw, const char* want_type, const Expect& expect) {
    std::string type, challenge, origin;
    if (!client_data(std::string(raw.begin(), raw.end()), type, challenge, origin)) return "The browser's client data is not readable";
    if (type != want_type) return "The browser answered a different request";
    if (challenge != expect.challenge) return "The request expired or was answered twice: try again";
    if (origin != expect.origin) return "The passkey answered for another address";
    return nullptr;
}

// The COSE key in an attested credential: an EC2 P-256 ES256 key.
const char* cose_key(Cbor& c, Credential& out) {
    int major;
    uint64_t n;
    if (!c.head(major, n) || major != 5) return "The passkey's key is not readable";
    int64_t kty = 0, alg = 0, crv = 0;
    bool have_x = false, have_y = false;
    for (uint64_t i = 0; i < n; ++i) {
        int64_t key;
        if (!c.integer(key)) return "The passkey's key is not readable";
        if (key == 1 || key == 3 || key == -1) {
            int64_t v;
            if (!c.integer(v)) return "The passkey's key is not readable";
            (key == 1 ? kty : key == 3 ? alg : crv) = v;
        } else if (key == -2 || key == -3) {
            const uint8_t* d;
            size_t len;
            if (!c.bytes(2, d, len) || len != 32) return "The passkey's key is not readable";
            std::memcpy(key == -2 ? out.x.data() : out.y.data(), d, 32);
            (key == -2 ? have_x : have_y) = true;
        } else if (!c.skip()) {
            return "The passkey's key is not readable";
        }
    }
    if (kty != 2 || alg != -7 || crv != 1 || !have_x || !have_y) return "This passkey uses a key type the dock does not support";
    return nullptr;
}

}  // namespace

std::string b64url(const uint8_t* data, size_t len) {
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        const uint32_t v = uint32_t(data[i]) << 16 | uint32_t(data[i + 1]) << 8 | data[i + 2];
        for (int s = 18; s >= 0; s -= 6) out += kAlphabet[(v >> s) & 63];
    }
    if (len - i == 1) {
        const uint32_t v = uint32_t(data[i]) << 16;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
    } else if (len - i == 2) {
        const uint32_t v = uint32_t(data[i]) << 16 | uint32_t(data[i + 1]) << 8;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
    }
    return out;
}

bool from_b64url(const std::string& s, Bytes& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-' || c == '+') v = 62;
        else if (c == '_' || c == '/') v = 63;
        else if (c == '=') break;
        else return false;
        acc = acc << 6 | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
        }
    }
    return bits < 6;  // a lone 6-bit group is not a whole byte: not base64
}

bool client_data(const std::string& json, std::string& type, std::string& challenge, std::string& origin) {
    Json j{json.data(), json.data() + json.size()};
    j.ws();
    if (j.p >= j.end || *j.p++ != '{') return false;
    bool t = false, c = false, o = false;
    for (;;) {
        std::string key;
        if (!j.str(key)) return false;
        j.ws();
        if (j.p >= j.end || *j.p++ != ':') return false;
        j.ws();
        if ((key == "type" || key == "challenge" || key == "origin") && j.p < j.end && *j.p == '"') {
            std::string& dst = key == "type" ? type : key == "challenge" ? challenge : origin;
            if (!j.str(dst)) return false;
            (key == "type" ? t : key == "challenge" ? c : o) = true;
        } else if (!j.value()) {
            return false;
        }
        j.ws();
        if (j.p >= j.end) return false;
        if (*j.p == ',') { ++j.p; continue; }
        if (*j.p == '}') break;
        return false;
    }
    return t && c && o;
}

const char* check_registration(const Bytes& client_data_json, const Bytes& attestation_object, const Expect& expect,
                               const Crypto& crypto, Credential& out) {
    if (const char* e = check_client(client_data_json, "webauthn.create", expect)) return e;

    Cbor c{attestation_object.data(), attestation_object.data() + attestation_object.size()};
    int major;
    uint64_t n;
    if (!c.head(major, n) || major != 5) return "The passkey's answer is not readable";
    const uint8_t* auth = nullptr;
    size_t auth_len = 0;
    for (uint64_t i = 0; i < n; ++i) {
        const uint8_t* k;
        size_t kl;
        if (!c.bytes(3, k, kl)) return "The passkey's answer is not readable";
        if (kl == 8 && std::memcmp(k, "authData", 8) == 0) {
            if (!c.bytes(2, auth, auth_len)) return "The passkey's answer is not readable";
        } else if (!c.skip()) {
            return "The passkey's answer is not readable";
        }
    }
    if (!auth || auth_len < 55) return "The passkey's answer has no key";
    if (!same(crypto.sha256(reinterpret_cast<const uint8_t*>(expect.rp_id.data()), expect.rp_id.size()), auth))
        return "The passkey was made for another address";
    const uint8_t flags = auth[32];
    if (!(flags & kUserPresent)) return "The passkey was not confirmed by you";
    if (!(flags & kAttestedData)) return "The passkey's answer has no key";
    const size_t id_len = size_t(auth[53]) << 8 | auth[54];
    if (id_len == 0 || id_len > 255 || auth_len < 55 + id_len) return "The passkey's id is not readable";
    out = Credential{};
    out.id.assign(auth + 55, auth + 55 + id_len);
    out.sign_count = be32(auth + 33);
    Cbor key{auth + 55 + id_len, auth + auth_len};
    return cose_key(key, out);
}

const char* check_assertion(const Credential& cred, const Bytes& client_data_json, const Bytes& authenticator_data,
                            const Bytes& signature, const Expect& expect, const Crypto& crypto, uint32_t& new_count) {
    if (const char* e = check_client(client_data_json, "webauthn.get", expect)) return e;
    if (authenticator_data.size() < 37) return "The passkey's answer is not readable";
    if (!same(crypto.sha256(reinterpret_cast<const uint8_t*>(expect.rp_id.data()), expect.rp_id.size()), authenticator_data.data()))
        return "The passkey is for another address";
    if (!(authenticator_data[32] & kUserPresent)) return "The passkey was not confirmed by you";
    new_count = be32(authenticator_data.data() + 33);
    // A counter that does not go up from a counting authenticator means a copied key.
    if ((cred.sign_count != 0 || new_count != 0) && new_count <= cred.sign_count) return "This passkey may have been copied: remove it and add it again";

    const Hash client_hash = crypto.sha256(client_data_json.data(), client_data_json.size());
    Bytes signed_data(authenticator_data);
    signed_data.insert(signed_data.end(), client_hash.begin(), client_hash.end());
    if (!crypto.verify(cred, crypto.sha256(signed_data.data(), signed_data.size()), signature)) return "The passkey's signature is wrong";
    return nullptr;
}

Bytes pack(const std::vector<Credential>& creds) {
    Bytes b{1, static_cast<uint8_t>(creds.size())};
    for (const auto& c : creds) {
        b.push_back(static_cast<uint8_t>(c.id.size()));
        b.insert(b.end(), c.id.begin(), c.id.end());
        b.insert(b.end(), c.x.begin(), c.x.end());
        b.insert(b.end(), c.y.begin(), c.y.end());
        for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(c.sign_count >> s));
        const size_t nl = c.name.size() > 64 ? 64 : c.name.size();
        b.push_back(static_cast<uint8_t>(nl));
        b.insert(b.end(), c.name.begin(), c.name.begin() + static_cast<long>(nl));
    }
    return b;
}

bool unpack(const Bytes& blob, std::vector<Credential>& out) {
    out.clear();
    if (blob.size() < 2 || blob[0] != 1) return false;
    size_t i = 2;
    for (int n = 0; n < blob[1]; ++n) {
        Credential c;
        if (i >= blob.size()) return false;
        const size_t il = blob[i++];
        if (blob.size() < i + il + 64 + 4 + 1) return false;
        c.id.assign(blob.begin() + static_cast<long>(i), blob.begin() + static_cast<long>(i + il));
        i += il;
        std::memcpy(c.x.data(), &blob[i], 32);
        std::memcpy(c.y.data(), &blob[i + 32], 32);
        i += 64;
        c.sign_count = be32(&blob[i]);
        i += 4;
        const size_t nl = blob[i++];
        if (blob.size() < i + nl) return false;
        c.name.assign(blob.begin() + static_cast<long>(i), blob.begin() + static_cast<long>(i + nl));
        i += nl;
        out.push_back(std::move(c));
    }
    return true;
}

}  // namespace webauthn
