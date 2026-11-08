#include "web.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

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

namespace web {
namespace {

constexpr char TAG[] = "web";
Hooks g_hooks;

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
    const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
    if (!slot) return result(req, false, "No update slot");
    if (req->content_len == 0 || req->content_len > slot->size) return result(req, false, "Image too large for the slot");
    esp_ota_handle_t ota;
    if (esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) return result(req, false, "Could not start the update");
    std::unique_ptr<char[]> buf(new char[4096]);
    size_t left = req->content_len;
    while (left > 0) {
        int n = httpd_req_recv(req, buf.get(), left < 4096 ? left : 4096);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0 || esp_ota_write(ota, buf.get(), static_cast<size_t>(n)) != ESP_OK) {
            esp_ota_abort(ota);
            return result(req, false, "Upload failed");
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

esp_err_t wifi_post(httpd_req_t* req) { return run_async(req, wifi_work); }
esp_err_t network_post(httpd_req_t* req) { return run_async(req, network_work); }
esp_err_t update_post(httpd_req_t* req) { return run_async(req, update_work); }

}  // namespace

esp_err_t start(const Hooks& hooks) {
    g_hooks = hooks;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;
    cfg.recv_wait_timeout = 40;  // a confirmation on the deck can take up to 30 s
    cfg.send_wait_timeout = 40;
    cfg.lru_purge_enable = true;
    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) return err;
    const httpd_uri_t uris[] = {
        {"/", HTTP_GET, index_get, nullptr},
        {"/api/status", HTTP_GET, status_get, nullptr},
        {"/api/scan", HTTP_GET, scan_get, nullptr},
        {"/api/wifi", HTTP_POST, wifi_post, nullptr},
        {"/api/network", HTTP_POST, network_post, nullptr},
        {"/api/update", HTTP_POST, update_post, nullptr},
        {"/trace", HTTP_GET, trace_get, nullptr},
        {"/api/key", HTTP_GET, key_get, nullptr},
    };
    for (const auto& u : uris) httpd_register_uri_handler(server, &u);
    ESP_LOGI(TAG, "web interface on port 80");
    return ESP_OK;
}

}  // namespace web
