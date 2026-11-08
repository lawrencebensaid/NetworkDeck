#include "net.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "board.hpp"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"

namespace net {
namespace {

constexpr char TAG[] = "net";
constexpr uint32_t kEthWaitMs = 5000;      // at boot, before falling back to Wi-Fi
constexpr uint32_t kWifiGiveUpMs = 20000;  // a saved network that won't connect: open the hotspot too

std::mutex g_mutex;  // guards the state below
Status g_status;
std::function<void(const Status&)> g_on_change;
std::string g_name;
settings::Network g_cfg;
settings::Wifi g_wifi_cfg;

esp_eth_handle_t g_eth = nullptr;
esp_netif_t* g_eth_netif = nullptr;
esp_netif_t* g_sta_netif = nullptr;
esp_netif_t* g_ap_netif = nullptr;
bool g_eth_link = false;
std::string g_eth_ip, g_sta_ip;
bool g_sta_connected = false;
bool g_wifi_started = false;
bool g_ap_on = false;
bool g_scanning = false;  // a scan asked for Wi-Fi: the policy must not switch it off meanwhile
uint32_t g_wifi_since = 0;  // when we started trying the saved network
TaskHandle_t g_task = nullptr;
SemaphoreHandle_t g_scan_done = nullptr;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
void wake() {
    if (g_task) xTaskNotifyGive(g_task);
}

std::string ip_str(const esp_ip4_addr_t& a) {
    char b[16];
    std::snprintf(b, sizeof(b), IPSTR, IP2STR(&a));
    return b;
}

settings::Ip4 ip_bytes(const esp_ip4_addr_t& ip) {
    return {esp_ip4_addr1(&ip), esp_ip4_addr2(&ip), esp_ip4_addr3(&ip), esp_ip4_addr4(&ip)};
}

esp_ip4_addr_t to_ip(const settings::Ip4& ip) {
    esp_ip4_addr_t a;
    IP4_ADDR(&a, ip[0], ip[1], ip[2], ip[3]);
    return a;
}

void apply_to(esp_netif_t* netif, const settings::Network& n) {
    if (!netif) return;
    if (n.dhcp) {
        esp_netif_dhcpc_start(netif);
        return;
    }
    esp_netif_dhcpc_stop(netif);
    esp_netif_ip_info_t info{};
    info.ip = to_ip(n.ip);
    info.netmask = to_ip(n.mask);
    info.gw = to_ip(n.gateway);
    esp_netif_set_ip_info(netif, &info);
    esp_netif_dns_info_t dns{};
    dns.ip.u_addr.ip4 = info.gw;
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
}

std::string random_password() {
    static const char kChars[] = "abcdefghjkmnpqrstuvwxyz23456789";  // nothing that looks alike
    std::string p;
    for (int i = 0; i < 8; ++i) p += kChars[esp_random() % (sizeof(kChars) - 1)];
    return p;
}

// ── Wi-Fi ───────────────────────────────────────────────────────────────────

bool wifi_init() {
    g_sta_netif = esp_netif_create_default_wifi_sta();
    g_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi unavailable: %s (is the C6 running esp-hosted?)", esp_err_to_name(err));
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    return true;
}

void wifi_mode(bool sta, bool ap) {
    if (!g_status.wifi_available) return;
    if (!sta && !ap) {
        if (g_wifi_started) {
            esp_wifi_stop();
            g_wifi_started = false;
            ESP_LOGI(TAG, "Wi-Fi off");
        }
        g_ap_on = false;
        g_sta_connected = false;
        g_sta_ip.clear();
        return;
    }
    const wifi_mode_t mode = sta && ap ? WIFI_MODE_APSTA : sta ? WIFI_MODE_STA : WIFI_MODE_AP;
    wifi_mode_t cur = WIFI_MODE_NULL;
    esp_wifi_get_mode(&cur);
    if (g_wifi_started && cur == mode) return;
    esp_wifi_set_mode(mode);
    if (sta) {
        wifi_config_t sc{};
        std::strncpy(reinterpret_cast<char*>(sc.sta.ssid), g_wifi_cfg.ssid.c_str(), sizeof(sc.sta.ssid) - 1);
        std::strncpy(reinterpret_cast<char*>(sc.sta.password), g_wifi_cfg.password.c_str(), sizeof(sc.sta.password) - 1);
        sc.sta.threshold.authmode = g_wifi_cfg.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        esp_wifi_set_config(WIFI_IF_STA, &sc);
    }
    if (ap) {
        if (g_status.hotspot_password.empty()) g_status.hotspot_password = random_password();
        g_status.hotspot_ssid = g_name;
        wifi_config_t ac{};
        std::strncpy(reinterpret_cast<char*>(ac.ap.ssid), g_name.c_str(), sizeof(ac.ap.ssid) - 1);
        ac.ap.ssid_len = static_cast<uint8_t>(g_name.size());
        std::strncpy(reinterpret_cast<char*>(ac.ap.password), g_status.hotspot_password.c_str(), sizeof(ac.ap.password) - 1);
        ac.ap.authmode = WIFI_AUTH_WPA2_PSK;
        ac.ap.max_connection = 4;
        ac.ap.channel = 6;
        esp_wifi_set_config(WIFI_IF_AP, &ac);
        g_status.hotspot_ip = "192.168.4.1";
    }
    if (!g_wifi_started) {
        esp_wifi_start();
        g_wifi_started = true;
    }
    g_ap_on = ap;
    if (sta) {
        apply_to(g_sta_netif, g_cfg);
        esp_wifi_connect();
        g_wifi_since = now_ms();
    }
    ESP_LOGI(TAG, "Wi-Fi %s%s%s", sta ? "joining '" : "", sta ? (g_wifi_cfg.ssid + "'").c_str() : "",
             ap ? (std::string(sta ? " + " : "") + "hotspot " + g_name).c_str() : "");
}

// ── events ──────────────────────────────────────────────────────────────────

void on_eth_event(void*, esp_event_base_t, int32_t id, void*) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Ethernet link up");
        g_eth_link = true;
    } else if (id == ETHERNET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "Ethernet link down");
        g_eth_link = false;
        g_eth_ip.clear();
    }
    wake();
}

void on_wifi_event(void*, esp_event_base_t, int32_t id, void*) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        g_sta_connected = false;
        g_sta_ip.clear();
        if (g_wifi_started && !g_wifi_cfg.ssid.empty() && !g_eth_link) esp_wifi_connect();  // keep trying
    } else if (id == WIFI_EVENT_STA_CONNECTED) {
        g_sta_connected = true;
        ESP_LOGI(TAG, "Wi-Fi joined '%s'", g_wifi_cfg.ssid.c_str());
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        if (g_scan_done) xSemaphoreGive(g_scan_done);
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "hotspot: a client joined");
    }
    wake();
}

void on_ip_event(void*, esp_event_base_t, int32_t id, void* data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto* ev = static_cast<ip_event_got_ip_t*>(data);
    if (id == IP_EVENT_ETH_GOT_IP) {
        g_eth_ip = ip_str(ev->ip_info.ip);
        ESP_LOGI(TAG, "Ethernet IP %s", g_eth_ip.c_str());
    } else if (id == IP_EVENT_STA_GOT_IP) {
        g_sta_ip = ip_str(ev->ip_info.ip);
        ESP_LOGI(TAG, "Wi-Fi IP %s", g_sta_ip.c_str());
    } else if (id == IP_EVENT_STA_LOST_IP) {
        g_sta_ip.clear();
    }
    wake();
}

// ── policy ──────────────────────────────────────────────────────────────────

void policy_locked(uint32_t boot) {
    const uint32_t t = now_ms();
    if (g_scanning) {
        // leave Wi-Fi as it is until the scan is done
    } else if (g_eth_link) {
        wifi_mode(false, false);
    } else if (t - boot >= kEthWaitMs) {
        const bool have_wifi = !g_wifi_cfg.ssid.empty();
        const bool sta_ok = g_sta_connected && !g_sta_ip.empty();
        // Hotspot when there's nothing to join, or the saved network hasn't worked for a while.
        const bool ap = !have_wifi || (!sta_ok && g_wifi_started && t - g_wifi_since >= kWifiGiveUpMs) ||
                        (g_ap_on && !sta_ok);
        wifi_mode(have_wifi, ap);
    }

    Status s = g_status;
    esp_netif_t* active_netif = nullptr;
    if (g_eth_link) {
        s.link = Link::Ethernet;
        s.ip = g_eth_ip;
        active_netif = g_eth_netif;
    } else if (g_sta_connected && !g_sta_ip.empty()) {
        s.link = Link::Wifi;
        s.ip = g_sta_ip;
        active_netif = g_sta_netif;
    } else if (g_ap_on) {
        s.link = Link::Hotspot;
        s.ip = g_status.hotspot_ip;
        active_netif = g_ap_netif;
    } else {
        s.link = Link::None;
        s.ip.clear();
    }
    // Report the actual active interface lease (also for static addresses), not a guessed /24.
    // Refresh even when the IP is unchanged: DHCP can renew with a different subnet mask.
    s.ip4 = {};
    s.netmask = {};
    esp_netif_ip_info_t info{};
    if (active_netif && !s.ip.empty() && esp_netif_get_ip_info(active_netif, &info) == ESP_OK) {
        s.ip4 = ip_bytes(info.ip);
        s.netmask = ip_bytes(info.netmask);
        s.ip = info.ip.addr ? ip_str(info.ip) : std::string();
    } else {
        s.ip.clear();  // no verified lease: do not display an old interface address
    }
    s.wifi_ssid = g_wifi_cfg.ssid;
    if (!g_ap_on) {
        s.hotspot_ssid.clear();
    }
    const bool changed = s.link != g_status.link || s.ip != g_status.ip || s.ip4 != g_status.ip4 ||
                         s.netmask != g_status.netmask || s.hotspot_ssid != g_status.hotspot_ssid ||
                         s.wifi_ssid != g_status.wifi_ssid;
    const std::string keep_pw = g_status.hotspot_password;
    g_status = s;
    g_status.hotspot_password = keep_pw;
    if (changed) {
        ESP_LOGI(TAG, "link %s, IP %s, advertised mask %u.%u.%u.%u", link_name(s.link), s.ip.empty() ? "-" : s.ip.c_str(),
                 s.netmask[0], s.netmask[1], s.netmask[2], s.netmask[3]);
        if (g_on_change) {
            Status copy = g_status;
            if (!g_ap_on) copy.hotspot_password.clear();
            g_mutex.unlock();
            g_on_change(copy);
            g_mutex.lock();
        }
    }
}

void task(void*) {
    const uint32_t boot = now_ms();
    // Bringing up the co-processor can take seconds: do it here, not on the boot path.
    const bool wifi = wifi_init();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_status.wifi_available = wifi;
    }
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        std::lock_guard<std::mutex> lock(g_mutex);
        policy_locked(boot);
    }
}

esp_err_t start_ethernet() {
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num = board::kEthMdc;
    emac_config.smi_gpio.mdio_num = board::kEthMdio;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = board::kEthRmiiClk;

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ESP_ETH_PHY_ADDR_AUTO;
    phy_config.reset_gpio_num = board::kEthPhyReset;

    esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    esp_eth_phy_t* phy = esp_eth_phy_new_generic(&phy_config);
    if (!mac || !phy) return ESP_FAIL;

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t err = esp_eth_driver_install(&config, &g_eth);
    if (err != ESP_OK) return err;

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    g_eth_netif = esp_netif_new(&netif_cfg);
    ESP_ERROR_CHECK(esp_netif_attach(g_eth_netif, esp_eth_new_netif_glue(g_eth)));
    apply_to(g_eth_netif, g_cfg);
    return esp_eth_start(g_eth);
}

}  // namespace

const char* link_name(Link l) {
    switch (l) {
        case Link::Ethernet: return "Ethernet";
        case Link::Wifi: return "Wi-Fi";
        case Link::Hotspot: return "Hotspot";
        default: return "No link";
    }
}

esp_err_t start(const std::string& name, std::function<void(const Status&)> on_change) {
    g_name = name;
    g_on_change = std::move(on_change);
    g_scan_done = xSemaphoreCreateBinary();
    g_cfg = settings::network();
    g_wifi_cfg = settings::wifi();
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, nullptr));

    err = start_ethernet();
    if (err != ESP_OK) ESP_LOGE(TAG, "Ethernet failed: %s", esp_err_to_name(err));
    xTaskCreatePinnedToCore(task, "net", 6144, nullptr, 6, &g_task, 1);
    return ESP_OK;
}

Status status() {
    std::lock_guard<std::mutex> lock(g_mutex);
    Status s = g_status;
    if (!g_ap_on) s.hotspot_password.clear();
    return s;
}

void apply(const settings::Network& n) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cfg = n;
    apply_to(g_eth_netif, n);
    if (g_wifi_started && !g_wifi_cfg.ssid.empty()) apply_to(g_sta_netif, n);
    ESP_LOGI(TAG, "addressing: %s", n.dhcp ? "DHCP" : "static");
    wake();
}

void join(const settings::Wifi& w) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_wifi_cfg = w;
    if (g_wifi_started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        g_wifi_started = false;
        g_sta_connected = false;
        g_sta_ip.clear();
    }
    wake();
}

std::vector<ScanResult> scan() {
    std::vector<ScanResult> out;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_status.wifi_available) return out;
        g_scanning = true;
        if (!g_wifi_started) {
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_start();
            g_wifi_started = true;
        }
    }
    wifi_scan_config_t sc{};
    xSemaphoreTake(g_scan_done, 0);
    // Non-blocking: over esp-hosted the blocking call returns before the results have arrived.
    if (esp_wifi_scan_start(&sc, false) != ESP_OK || xSemaphoreTake(g_scan_done, pdMS_TO_TICKS(15000)) != pdTRUE) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_scanning = false;
        wake();
        return out;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    std::vector<wifi_ap_record_t> recs(n);
    esp_wifi_scan_get_ap_records(&n, recs.data());
    for (uint16_t i = 0; i < n; ++i) {
        std::string ssid(reinterpret_cast<const char*>(recs[i].ssid));
        if (ssid.empty()) continue;
        auto it = std::find_if(out.begin(), out.end(), [&](const ScanResult& r) { return r.ssid == ssid; });
        if (it != out.end()) continue;  // strongest first: keep the first record of each name
        out.push_back({ssid, recs[i].rssi, recs[i].authmode != WIFI_AUTH_OPEN});
    }
    {
        // If we started Wi-Fi only to scan, the policy task turns it off again on its next pass.
        std::lock_guard<std::mutex> lock(g_mutex);
        g_scanning = false;
        wake();
    }
    return out;
}

std::array<uint8_t, 6> mac() {
    std::array<uint8_t, 6> m{};
    esp_read_mac(m.data(), ESP_MAC_ETH);
    return m;
}

std::string ip() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_status.ip;
}

esp_err_t start_mdns(const std::string& name, const std::string& serial, uint16_t port) {
    esp_err_t err = mdns_init();
    if (err != ESP_OK) return err;
    std::string host;
    for (char c : name) host.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    mdns_hostname_set(host.c_str());
    mdns_instance_name_set(name.c_str());
    mdns_txt_item_t txt[] = {
        {"dt", "215"},
        {"sn", serial.c_str()},
    };
    return mdns_service_add(name.c_str(), "_elg", "_tcp", port, txt, sizeof(txt) / sizeof(txt[0]));
}

}  // namespace net
