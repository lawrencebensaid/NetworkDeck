#include "web.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <array>
#include <algorithm>
#include <atomic>
#include <mutex>
#include "auth_policy.hpp"
#include "esp_timer.h"
#include "nvs.h"
#include "psa/crypto.h"

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

extern const char login_html_start[] asm("_binary_login_html_start");
extern const char login_html_end[] asm("_binary_login_html_end");

namespace web {
namespace {

constexpr char TAG[] = "web";
Hooks g_hooks;
std::mutex g_auth_mutex;
std::atomic<bool> g_login_busy{false};
std::atomic<bool> g_update_busy{false};
std::array<uint8_t, 49> g_credential{};  // version, 16-byte salt, 32-byte PBKDF2 hash
bool g_configured = false;
bool g_auth_error = false;
std::string g_session;
int64_t g_expires = 0;
int64_t g_next_login = 0;

std::string header(httpd_req_t* req, const char* name) {
    const size_t size = httpd_req_get_hdr_value_len(req, name);
    if (size == 0 || size > 1024) return {};
    std::string value(size + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK) return {};
    value.resize(size);
    return value;
}

void private_headers(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "frame-ancestors 'none'; base-uri 'none'; object-src 'none'");
}

bool authenticated(httpd_req_t* req) {
    const auto cookies = header(req, "Cookie");
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    return !g_auth_error && g_configured && auth::session_valid(g_session, g_expires, cookies, esp_timer_get_time());
}

void load_auth() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("deck32", NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return;
    if (err != ESP_OK) { g_auth_error = true; return; }
    size_t size = g_credential.size();
    err = nvs_get_blob(h, "web_auth", g_credential.data(), &size);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return;
    g_auth_error = err != ESP_OK || size != g_credential.size() || g_credential[0] != 1;
    g_configured = !g_auth_error;
}

bool derive(const std::string& password, std::array<uint8_t, 49>& record) {
    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t status = psa_key_derivation_setup(&op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS) status = psa_key_derivation_input_integer(&op, PSA_KEY_DERIVATION_INPUT_COST, 100000);
    if (status == PSA_SUCCESS) status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT, record.data() + 1, 16);
    if (status == PSA_SUCCESS) status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_PASSWORD,
        reinterpret_cast<const uint8_t*>(password.data()), password.size());
    if (status == PSA_SUCCESS) status = psa_key_derivation_output_bytes(&op, record.data() + 17, 32);
    psa_key_derivation_abort(&op);
    return status == PSA_SUCCESS;
}

esp_err_t denied(httpd_req_t* req, const char* status, const char* message) {
    private_headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    // Reject before reading a potentially large body; never reuse that connection.
    httpd_resp_set_hdr(req, "Connection", "close");
    const auto err = httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    return err;
}

bool browser_write(httpd_req_t* req) {
    // Cross-origin HTML forms cannot set this header. No CORS/preflight endpoint is exposed.
    return header(req, "X-NetworkDeck") == "1";
}

template<esp_err_t (*Handler)(httpd_req_t*)>
esp_err_t protected_handler(httpd_req_t* req) {
    private_headers(req);
    if (!authenticated(req)) return denied(req, "401 Unauthorized", "{\"ok\":false,\"message\":\"Sign in first\"}");
    if (req->method != HTTP_GET && !browser_write(req))
        return denied(req, "403 Forbidden", "{\"ok\":false,\"message\":\"Invalid request origin\"}");
    return Handler(req);
}

// Requests from the setup hotspot (192.168.4.x) need no confirmation: its password is only shown on
// the deck, so whoever is on it is standing next to the dock.
bool from_hotspot(httpd_req_t* req) {
    sockaddr_in6 addr{};
    socklen_t len = sizeof(addr);
    if (getpeername(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
    uint32_t ip4 = 0;
    if (addr.sin6_family == AF_INET) {
        ip4 = reinterpret_cast<sockaddr_in*>(&addr)->sin_addr.s_addr;
    } else if (addr.sin6_family == AF_INET6) {
        ip4 = addr.sin6_addr.un.u32_addr[3];  // IPv4-mapped
    }
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&ip4);
    return b[0] == 192 && b[1] == 168 && b[2] == 4;
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        o += c;
    }
    return o;
}

esp_err_t send_json(httpd_req_t* req, const std::string& body, const char* status = "200 OK") {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body.c_str(), static_cast<ssize_t>(body.size()));
}

esp_err_t result(httpd_req_t* req, bool ok, const std::string& message) {
    return send_json(req, std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + json_escape(message) + "\"}",
                     ok ? "200 OK" : "409 Conflict");
}

// Reads an application/x-www-form-urlencoded body (small forms only).
bool read_form(httpd_req_t* req, std::string& body) {
    if (req->content_len > 2048) return false;
    body.resize(req->content_len);
    size_t got = 0;
    while (got < body.size()) {
        int n = httpd_req_recv(req, body.data() + got, body.size() - got);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

std::string url_decode(const char* s) {
    std::string o;
    for (; *s; ++s) {
        if (*s == '+') {
            o += ' ';
        } else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], 0};
            o += static_cast<char>(std::strtol(hex, nullptr, 16));
            s += 2;
        } else {
            o += *s;
        }
    }
    return o;
}

std::string field(const std::string& body, const char* key) {
    char buf[256];
    if (httpd_query_key_value(body.c_str(), key, buf, sizeof(buf)) != ESP_OK) return {};
    return url_decode(buf);
}

bool parse_ip(const std::string& s, settings::Ip4& out) {
    unsigned a, b, c, d;
    char tail;
    if (std::sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    out = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d)};
    return true;
}

bool confirmed(httpd_req_t* req, const std::string& question) {
    if (from_hotspot(req)) return true;
    return g_hooks.confirm && g_hooks.confirm(question);
}

// ── handlers ────────────────────────────────────────────────────────────────

esp_err_t index_get(httpd_req_t* req) {
    private_headers(req);
    if (!authenticated(req)) {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        if (g_auth_error) return denied(req, "503 Service Unavailable", "{\"ok\":false,\"message\":\"Authentication storage unavailable; recover with physical factory reset\"}");
        std::string page(login_html_start, login_html_end - login_html_start - 1);
        const auto at = page.find("AUTH_MODE");
        if (at != std::string::npos) page.replace(at, 9, g_configured ? "false" : "true");
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, page.data(), page.size());
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

esp_err_t status_get(httpd_req_t* req) {
    std::string j = g_hooks.status_json ? g_hooks.status_json() : "{}";
    j.insert(1, std::string("\"via_hotspot\":") + (from_hotspot(req) ? "true" : "false") + (j.size() > 2 ? "," : ""));
    return send_json(req, j);
}

esp_err_t scan_get(httpd_req_t* req) {
    std::string out = "[";
    if (g_hooks.scan) {
        bool first = true;
        for (const auto& n : g_hooks.scan()) {
            if (!first) out += ",";
            first = false;
            out += "{\"ssid\":\"" + json_escape(n.ssid) + "\",\"rssi\":" + std::to_string(n.rssi) +
                   ",\"secure\":" + (n.secure ? "true" : "false") + "}";
        }
    }
    out += "]";
    return send_json(req, out);
}

esp_err_t trace_get(httpd_req_t* req) {
    const std::string t = g_hooks.trace ? g_hooks.trace() : "";
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, t.c_str(), static_cast<ssize_t>(t.size()));
}

esp_err_t key_get(httpd_req_t* req) {
    char q[32], v[8];
    int key = -1;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && httpd_query_key_value(q, "n", v, sizeof(v)) == ESP_OK)
        key = std::atoi(v);
    std::string type;
    auto img = g_hooks.key_image ? g_hooks.key_image(key, type) : std::vector<uint8_t>{};
    if (img.empty()) {
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_send(req, nullptr, 0);
    }
    httpd_resp_set_type(req, type.c_str());
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, reinterpret_cast<const char*>(img.data()), static_cast<ssize_t>(img.size()));
}

esp_err_t wifi_work(httpd_req_t* req) {
    std::string body;
    if (!read_form(req, body)) return result(req, false, "Bad request");
    settings::Wifi w{field(body, "ssid"), field(body, "password")};
    if (w.ssid.empty()) return result(req, false, "Choose a network");
    if (!confirmed(req, "Join Wi-Fi '" + w.ssid + "'?")) return result(req, false, "Not confirmed on the deck");
    if (g_hooks.set_wifi) g_hooks.set_wifi(w);
    return result(req, true, "Joining " + w.ssid);
}

esp_err_t network_work(httpd_req_t* req) {
    std::string body;
    if (!read_form(req, body)) return result(req, false, "Bad request");
    settings::Network n;
    n.dhcp = field(body, "mode") != "static";
    if (!n.dhcp) {
        if (!parse_ip(field(body, "ip"), n.ip) || !parse_ip(field(body, "mask"), n.mask) ||
            !parse_ip(field(body, "gateway"), n.gateway))
            return result(req, false, "Enter IP address, netmask and gateway as a.b.c.d");
    }
    char ipb[16];
    std::snprintf(ipb, sizeof(ipb), "%u.%u.%u.%u", n.ip[0], n.ip[1], n.ip[2], n.ip[3]);
    if (!confirmed(req, n.dhcp ? std::string("Use DHCP?") : std::string("Use static IP ") + ipb + "?"))
        return result(req, false, "Not confirmed on the deck");
    if (g_hooks.set_network) g_hooks.set_network(n);
    return result(req, true, n.dhcp ? "Using DHCP" : std::string("Using ") + ipb);
}

void restart_soon(void*) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

// The image is written to the spare slot first; only after it validates and is confirmed on the deck
// does the dock boot from it. The bootloader rolls back if it fails to start its network.
esp_err_t update_work(httpd_req_t* req) {
    // One OTA writer only, including validation and physical confirmation.
    if (g_update_busy.exchange(true)) return denied(req, "409 Conflict", "{\"ok\":false,\"message\":\"An update is already running\"}");
    struct Release { ~Release() { g_update_busy = false; } } release;
    if (!authenticated(req)) return denied(req, "401 Unauthorized", "{\"ok\":false,\"message\":\"Sign in first\"}");
    const int64_t deadline = esp_timer_get_time() + 120LL * 1000000;
    const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
    if (!slot) return denied(req, "409 Conflict", "{\"ok\":false,\"message\":\"No update slot\"}");
    if (req->content_len == 0 || req->content_len > slot->size) return denied(req, "413 Content Too Large", "{\"ok\":false,\"message\":\"Empty image or image too large for the slot\"}");
    esp_ota_handle_t ota;
    if (esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) return denied(req, "409 Conflict", "{\"ok\":false,\"message\":\"Could not start the update\"}");
    std::unique_ptr<char[]> buf(new char[4096]);
    size_t left = req->content_len;
    while (left > 0) {
        if (esp_timer_get_time() >= deadline) {
            esp_ota_abort(ota);
            return denied(req, "408 Request Timeout", "{\"ok\":false,\"message\":\"Upload timed out\"}");
        }
        int n = httpd_req_recv(req, buf.get(), left < 4096 ? left : 4096);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0 || esp_ota_write(ota, buf.get(), static_cast<size_t>(n)) != ESP_OK) {
            esp_ota_abort(ota);
            return denied(req, "400 Bad Request", "{\"ok\":false,\"message\":\"Upload failed\"}");
        }
        left -= static_cast<size_t>(n);
    }
    if (esp_ota_end(ota) != ESP_OK) return result(req, false, "Not a valid Deck32 firmware image");
    esp_app_desc_t desc{};
    esp_ota_get_partition_description(slot, &desc);
    if (std::strcmp(desc.project_name, esp_app_get_description()->project_name) != 0)
        return result(req, false, std::string("This image is for '") + desc.project_name + "'");
    if (!confirmed(req, std::string("Install firmware ") + desc.version + "?")) return result(req, false, "Not confirmed on the deck");
    if (esp_ota_set_boot_partition(slot) != ESP_OK) return result(req, false, "Could not select the new image");
    ESP_LOGW(TAG, "firmware %s installed, restarting", desc.version);
    xTaskCreate(restart_soon, "restart", 2048, nullptr, 5, nullptr);
    return result(req, true, std::string("Installed ") + desc.version + ", restarting");
}

// Changes can wait up to 30 s for an answer on the deck. The server has a single task, so those
// requests are finished on a worker task and the rest of the interface keeps answering meanwhile.
struct Job {
    httpd_req_t* req;
    esp_err_t (*work)(httpd_req_t*);
};

void job_task(void* arg) {
    auto* job = static_cast<Job*>(arg);
    job->work(job->req);
    httpd_req_async_handler_complete(job->req);
    delete job;
    vTaskDelete(nullptr);
}

esp_err_t run_async(httpd_req_t* req, esp_err_t (*work)(httpd_req_t*)) {
    httpd_req_t* copy = nullptr;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) return work(req);
    auto* job = new Job{copy, work};
    if (xTaskCreate(job_task, "web_job", 8192, job, 5, nullptr) != pdPASS) {
        delete job;
        esp_err_t err = work(copy);
        httpd_req_async_handler_complete(copy);
        return err;
    }
    return ESP_OK;
}

esp_err_t login_work(httpd_req_t* req) {
    struct Release { ~Release() { g_login_busy = false; } } release;
    private_headers(req);
    std::string password;
    // A volatile wipe also covers early returns; do not leave the plaintext in a freed string buffer.
    struct Wipe {
        std::string& value;
        ~Wipe() { for (size_t i = 0; i < value.size(); ++i) *reinterpret_cast<volatile char*>(&value[i]) = 0; }
    } wipe{password};
    if (!read_form(req, password) || !auth::password_valid(password))
        return result(req, false, "Use a password of 12–128 bytes");
    std::array<uint8_t, 49> record;
    bool setup;
    {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        if (g_auth_error) return denied(req, "503 Service Unavailable", "{\"ok\":false,\"message\":\"Authentication unavailable\"}");
        setup = !g_configured;
        record = g_credential;
    }
    if (setup) {
        // No hotspot exception: initial ownership always requires physical approval.
        if (!g_hooks.confirm || !g_hooks.confirm("Set web interface password?"))
            return result(req, false, "Not confirmed on the deck");
        record[0] = 1;
        if (psa_generate_random(record.data() + 1, 16) != PSA_SUCCESS)
            return result(req, false, "Could not generate password salt");
    }
    const bool derived = derive(password, record);
    std::fill(password.begin(), password.end(), '\0');
    if (!derived) return result(req, false, "Password hashing unavailable");
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    if (!setup && !auth::constant_equal(std::string(reinterpret_cast<const char*>(record.data() + 17), 32),
                                       std::string(reinterpret_cast<const char*>(g_credential.data() + 17), 32)))
        return denied(req, "401 Unauthorized", "{\"ok\":false,\"message\":\"Incorrect password\"}");
    uint8_t random[32];
    if (psa_generate_random(random, sizeof(random)) != PSA_SUCCESS) return result(req, false, "Session unavailable");
    if (setup) {
        nvs_handle_t h;
        if (nvs_open("deck32", NVS_READWRITE, &h) != ESP_OK) return result(req, false, "Could not save password");
        esp_err_t err = nvs_set_blob(h, "web_auth", record.data(), record.size());
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
        if (err != ESP_OK) return result(req, false, "Could not save password");
        g_credential = record;
        g_configured = true;
    }
    constexpr char hex[] = "0123456789abcdef";
    g_session.clear();
    for (auto b : random) { g_session += hex[b >> 4]; g_session += hex[b & 15]; }
    g_expires = esp_timer_get_time() + auth::kLifetimeUs;
    const auto cookie = "nd_session=" + g_session + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=1800";
    httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
    return result(req, true, "Signed in");
}

esp_err_t login_post(httpd_req_t* req) {
    if (!browser_write(req)) return denied(req, "403 Forbidden", "{\"ok\":false,\"message\":\"Invalid request origin\"}");
    if (req->content_len < 12 || req->content_len > 128)
        return denied(req, "400 Bad Request", "{\"ok\":false,\"message\":\"Use a password of 12–128 bytes\"}");
    {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        if (esp_timer_get_time() < g_next_login || g_login_busy.exchange(true))
            return denied(req, "429 Too Many Requests", "{\"ok\":false,\"message\":\"Wait before trying again\"}");
        g_next_login = esp_timer_get_time() + auth::kRetryUs;
    }
    return run_async(req, login_work);
}

esp_err_t logout_post(httpd_req_t* req) {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    g_session.clear();
    g_expires = 0;
    httpd_resp_set_hdr(req, "Set-Cookie", "nd_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
    return result(req, true, "Signed out");
}

esp_err_t wifi_post(httpd_req_t* req) { return run_async(req, wifi_work); }
esp_err_t network_post(httpd_req_t* req) { return run_async(req, network_work); }
esp_err_t update_post(httpd_req_t* req) { return run_async(req, update_work); }

}  // namespace

esp_err_t start(const Hooks& hooks) {
    g_hooks = hooks;
    if (psa_crypto_init() != PSA_SUCCESS) return ESP_FAIL;
    load_auth();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;
    cfg.max_resp_headers = 16;  // security headers, cookie and error/JSON headers
    cfg.recv_wait_timeout = 40;  // a confirmation on the deck can take up to 30 s
    cfg.send_wait_timeout = 40;
    cfg.lru_purge_enable = true;
    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) return err;
    const httpd_uri_t uris[] = {
        {"/", HTTP_GET, index_get, nullptr},
        {"/api/login", HTTP_POST, login_post, nullptr},
        {"/api/logout", HTTP_POST, protected_handler<logout_post>, nullptr},
        {"/api/status", HTTP_GET, protected_handler<status_get>, nullptr},
        {"/api/scan", HTTP_GET, protected_handler<scan_get>, nullptr},
        {"/api/wifi", HTTP_POST, protected_handler<wifi_post>, nullptr},
        {"/api/network", HTTP_POST, protected_handler<network_post>, nullptr},
        {"/api/update", HTTP_POST, protected_handler<update_post>, nullptr},
        {"/trace", HTTP_GET, protected_handler<trace_get>, nullptr},
        {"/api/key", HTTP_GET, protected_handler<key_get>, nullptr},
    };
    for (const auto& u : uris) {
        err = httpd_register_uri_handler(server, &u);
        if (err != ESP_OK) { httpd_stop(server); return err; }
    }
    ESP_LOGI(TAG, "web interface on port 80");
    return ESP_OK;
}

}  // namespace web
