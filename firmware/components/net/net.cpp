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
#include "lwip/sockets.h"
#include "mdns.h"
#include "eventlog.hpp"

namespace net {
namespace {

constexpr char TAG[] = "net";
constexpr uint32_t kEthWaitMs = 5000;      // at boot, before falling back to Wi-Fi
constexpr uint32_t kWifiGiveUpMs = 20000;  // a saved network that won't connect: open the hotspot too
constexpr uint32_t kRetryMs = 5000;        // after a failed attempt to join, before the next
constexpr uint32_t kAttemptMs = 20000;     // an attempt with no answer at all counts as failed
constexpr uint32_t kHotspotOpenMs = 15 * 60 * 1000;  // the setup hotspot asked for on the deck

std::mutex g_mutex;  // guards the state below
Status g_status;
std::function<void(const Status&)> g_on_change;
std::string g_name;
std::string g_hostname;  // mDNS and DHCP
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
bool g_sta_wanted = false;  // the station should be joining the saved network
bool g_sta_started = false; // WIFI_EVENT_STA_START seen: connecting is allowed
uint32_t g_last_try = 0;    // last esp_wifi_connect(), or when it failed
bool g_connecting = false;  // an attempt is under way: never start another on top (it aborts it)
uint32_t g_hotspot_until = 0;  // the hotspot is held open until then (0: not asked for)
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

// The setup network: "NetworkDeck ESP32-P4", after the chip the firmware runs on.
std::string hotspot_name() { return std::string("NetworkDeck ") + chip_name(); }

void try_connect() {
    if (!g_sta_wanted || !g_sta_started || g_sta_connected || g_scanning) return;  // a connect aborts a scan
    g_last_try = now_ms();
    g_connecting = true;
    if (esp_err_t e = esp_wifi_connect(); e != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect: %s", esp_err_to_name(e));
        g_connecting = false;
    }
}

bool wifi_init() {
    g_sta_netif = esp_netif_create_default_wifi_sta();
    g_ap_netif = esp_netif_create_default_wifi_ap();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_hostname.empty()) esp_netif_set_hostname(g_sta_netif, g_hostname.c_str());
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi unavailable: %s (on the P4: is the co-processor running esp-hosted?)", esp_err_to_name(err));
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    // The C5 does 2.4 and 5 GHz: look for the saved network on both (a 5 GHz-only network is
    // otherwise never found).
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_SOC_WIFI_SUPPORT_5G
    wifi_band_mode_t band = WIFI_BAND_MODE_2G_ONLY;
    esp_wifi_get_band_mode(&band);
    if (band != WIFI_BAND_MODE_AUTO) {
        err = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
        ESP_LOGI(TAG, "Wi-Fi bands: was %d, now 2.4 + 5 GHz (%s)", static_cast<int>(band), esp_err_to_name(err));
    }
#endif
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
        g_sta_wanted = false;
        g_sta_started = false;
        g_connecting = false;
        g_ap_on = false;
        g_sta_connected = false;
        g_sta_ip.clear();
        return;
    }
    const wifi_mode_t mode = sta && ap ? WIFI_MODE_APSTA : sta ? WIFI_MODE_STA : WIFI_MODE_AP;
    wifi_mode_t cur = WIFI_MODE_NULL;
    esp_wifi_get_mode(&cur);
    if (g_wifi_started && cur == mode && g_sta_wanted == sta) return;
    if (esp_err_t e = esp_wifi_set_mode(mode); e != ESP_OK) ESP_LOGW(TAG, "Wi-Fi mode: %s", esp_err_to_name(e));
    if (sta) {
        wifi_config_t sc{};
        std::strncpy(reinterpret_cast<char*>(sc.sta.ssid), g_wifi_cfg.ssid.c_str(), sizeof(sc.sta.ssid) - 1);
        std::strncpy(reinterpret_cast<char*>(sc.sta.password), g_wifi_cfg.password.c_str(), sizeof(sc.sta.password) - 1);
        sc.sta.threshold.authmode = g_wifi_cfg.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        // Every channel on both bands, then the strongest access point of the network (the fast scan
        // stops at the first it hears, which may be a weak one or miss a 5 GHz one).
        sc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        sc.sta.pmf_cfg.capable = true;
        esp_wifi_set_config(WIFI_IF_STA, &sc);
    }
    if (ap) {
        if (g_status.hotspot_password.empty()) g_status.hotspot_password = random_password();
        const std::string ssid = hotspot_name();
        g_status.hotspot_ssid = ssid;
        wifi_config_t ac{};
        std::strncpy(reinterpret_cast<char*>(ac.ap.ssid), ssid.c_str(), sizeof(ac.ap.ssid) - 1);
        ac.ap.ssid_len = static_cast<uint8_t>(ssid.size());
        std::strncpy(reinterpret_cast<char*>(ac.ap.password), g_status.hotspot_password.c_str(), sizeof(ac.ap.password) - 1);
        ac.ap.authmode = WIFI_AUTH_WPA2_PSK;
        ac.ap.max_connection = 4;
        ac.ap.channel = 6;
        esp_wifi_set_config(WIFI_IF_AP, &ac);
        g_status.hotspot_ip = "192.168.4.1";
    }
    const bool was_sta = g_sta_wanted;
    g_sta_wanted = sta;
    g_ap_on = ap;
    if (sta && !was_sta) {
        apply_to(g_sta_netif, g_cfg);
        g_wifi_since = now_ms();
    }
    if (!g_wifi_started) {
        // Connecting waits for WIFI_EVENT_STA_START: over esp-hosted the start completes later.
        g_sta_started = false;
        g_connecting = false;
        if (esp_err_t e = esp_wifi_start(); e != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi start: %s", esp_err_to_name(e));
            return;
        }
        g_wifi_started = true;
    } else if (sta && !was_sta) {
        try_connect();
    }
    ESP_LOGI(TAG, "Wi-Fi %s%s%s", sta ? "joining '" : "", sta ? (g_wifi_cfg.ssid + "'").c_str() : "",
             ap ? (std::string(sta ? " + " : "") + "hotspot " + hotspot_name()).c_str() : "");
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

void on_wifi_event(void*, esp_event_base_t, int32_t id, void* data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* d = static_cast<const wifi_event_sta_disconnected_t*>(data);
        if (g_sta_wanted) {
            const unsigned reason = d ? d->reason : 0;
            ESP_LOGW(TAG, "Wi-Fi '%s': not joined (reason %u, signal %d dBm)", g_wifi_cfg.ssid.c_str(), reason, d ? d->rssi : 0);
            static unsigned last_reason = 0;
            if (reason != last_reason || g_sta_connected) {  // once per kind of failure, not every retry
                last_reason = reason;
                const char* why = reason == 201 ? "the network was not found"
                                  : (reason == 15 || reason == 2 || reason == 204 || reason == 202) ? "the password may be wrong"
                                  : reason == 8 ? "it left the network"
                                  : "the access point did not accept it";
                eventlog::add(eventlog::Cat::Network, eventlog::Level::Warn, "Wi-Fi '%s' not joined: %s (reason %u)",
                              g_wifi_cfg.ssid.c_str(), why, reason);
            }
        }

        g_sta_connected = false;
        g_connecting = false;
        g_last_try = now_ms();
        g_sta_ip.clear();
        // The policy task tries again every few seconds.
    } else if (id == WIFI_EVENT_STA_START) {
        g_sta_started = true;
        g_last_try = now_ms() - kRetryMs;  // the policy task connects at once (not here: a small stack)
    } else if (id == WIFI_EVENT_STA_STOP) {
        g_sta_started = false;
        g_connecting = false;
    } else if (id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "hotspot %s is up", hotspot_name().c_str());
        // In AP-only mode there is no STA_START: nothing to connect.
    } else if (id == WIFI_EVENT_STA_CONNECTED) {
        g_sta_connected = true;
        g_connecting = false;
        ESP_LOGI(TAG, "Wi-Fi joined '%s'", g_wifi_cfg.ssid.c_str());
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        if (g_scan_done) xSemaphoreGive(g_scan_done);
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "hotspot: a client joined");
    }
    wake();
}

// Matter runs over IPv6: an interface that is up gets its link-local address (SLAAC adds the rest).
// On the IPv4 event, because that comes after the interface is up, with DHCP or a static address.
void add_ip6_linklocal(esp_netif_t* netif, const char* what) {
    if (esp_err_t e = esp_netif_create_ip6_linklocal(netif); e != ESP_OK)
        ESP_LOGW(TAG, "%s: no IPv6 link-local address (%s)", what, esp_err_to_name(e));
}

void on_ip_event(void*, esp_event_base_t, int32_t id, void* data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto* ev = static_cast<ip_event_got_ip_t*>(data);
    if (id == IP_EVENT_ETH_GOT_IP) {
        g_eth_ip = ip_str(ev->ip_info.ip);
        ESP_LOGI(TAG, "Ethernet IP %s", g_eth_ip.c_str());
        add_ip6_linklocal(g_eth_netif, "Ethernet");
    } else if (id == IP_EVENT_STA_GOT_IP) {
        g_sta_ip = ip_str(ev->ip_info.ip);
        ESP_LOGI(TAG, "Wi-Fi IP %s", g_sta_ip.c_str());
        add_ip6_linklocal(g_sta_netif, "Wi-Fi");
    } else if (id == IP_EVENT_GOT_IP6) {
        const auto* ev6 = static_cast<ip_event_got_ip6_t*>(data);
        ESP_LOGI(TAG, "IPv6 " IPV6STR, IPV62STR(ev6->ip6_info.ip));
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
    } else if (!g_wifi_cfg.enabled) {
        wifi_mode(false, false);
    } else {
        if (g_hotspot_until && static_cast<int32_t>(t - g_hotspot_until) >= 0) g_hotspot_until = 0;
        const bool asked = g_hotspot_until != 0;  // opened on the deck: also while Ethernet is up
        const bool have_wifi = !g_wifi_cfg.ssid.empty();
        const bool sta_ok = g_sta_connected && !g_sta_ip.empty();
        if (g_eth_link) {
            wifi_mode(asked && have_wifi, asked);
        } else if (t - boot >= kEthWaitMs || asked) {
            // Hotspot when there's nothing to join, or the saved network hasn't worked for a while.
            const bool ap = asked || !have_wifi || (!sta_ok && g_sta_wanted && t - g_wifi_since >= kWifiGiveUpMs) ||
                            (g_ap_on && !sta_ok);
            wifi_mode(have_wifi, ap);
        }
        if (g_sta_wanted && g_sta_started && !g_sta_connected && t - g_last_try >= (g_connecting ? kAttemptMs : kRetryMs))
            try_connect();
    }

    Status s = g_status;
    if (g_eth_link && !g_eth_ip.empty()) {
        s.link = Link::Ethernet;
        s.ip = g_eth_ip;
    } else if (g_sta_connected && !g_sta_ip.empty()) {
        s.link = Link::Wifi;
        s.ip = g_sta_ip;
    } else if (g_ap_on) {
        s.link = Link::Hotspot;
        s.ip = g_status.hotspot_ip;
    } else if (g_eth_link) {
        s.link = Link::Ethernet;  // link up, still waiting for an address
        s.ip.clear();
    } else {
        s.link = Link::None;
        s.ip.clear();
    }
    s.wifi_ssid = g_wifi_cfg.ssid;
    s.wifi_enabled = g_wifi_cfg.enabled;
    if (!g_ap_on) {
        s.hotspot_ssid.clear();
    }
    const bool changed = s.link != g_status.link || s.ip != g_status.ip || s.hotspot_ssid != g_status.hotspot_ssid ||
                         s.wifi_ssid != g_status.wifi_ssid || s.wifi_enabled != g_status.wifi_enabled;
    const std::string keep_pw = g_status.hotspot_password;
    g_status = s;
    g_status.hotspot_password = keep_pw;
    if (changed) {
        ESP_LOGI(TAG, "link %s, IP %s", link_name(s.link), s.ip.empty() ? "-" : s.ip.c_str());
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

#if CONFIG_ETH_USE_ESP32_EMAC
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
#else
esp_err_t start_ethernet() { return ESP_ERR_NOT_SUPPORTED; }  // no Ethernet on this chip: Wi-Fi only
#endif

}  // namespace

const char* chip_name() {
    // CONFIG_IDF_TARGET "esp32p4" -> "ESP32-P4"
    static char name[16] = "";
    if (!name[0]) {
        const char* t = CONFIG_IDF_TARGET;
        size_t n = 0;
        for (size_t i = 0; t[i] && n + 2 < sizeof(name); ++i) {
            if (i == 5) name[n++] = '-';
            name[n++] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[i])));
        }
        name[n] = '\0';
    }
    return name;
}

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
    if (err == ESP_ERR_NOT_SUPPORTED) ESP_LOGI(TAG, "no Ethernet on this chip");
    else if (err != ESP_OK) ESP_LOGE(TAG, "Ethernet failed: %s", esp_err_to_name(err));
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
        g_sta_wanted = false;
        g_sta_started = false;
        g_connecting = false;
        g_sta_connected = false;
        g_sta_ip.clear();
        g_ap_on = false;
    }
    wake();
}

void open_hotspot(bool open) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hotspot_until = open ? (now_ms() + kHotspotOpenMs) | 1u : 0;
    wake();
}

std::vector<ScanResult> scan() {
    // One scan at a time: a second caller waits and then scans again (two pages open, say).
    static std::mutex one_at_a_time;
    std::lock_guard<std::mutex> serial(one_at_a_time);
    std::vector<ScanResult> out;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_status.wifi_available || !g_wifi_cfg.enabled) return out;
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

void set_hostname(const std::string& host) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hostname = host;
    if (g_eth_netif) esp_netif_set_hostname(g_eth_netif, host.c_str());
    if (g_sta_netif) esp_netif_set_hostname(g_sta_netif, host.c_str());
    mdns_hostname_set(host.c_str());  // before mdns_init it fails harmlessly; start_mdns sets it then
}

std::string hostname() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_hostname;
}

namespace {

// One UDP question and its answer (or nothing within `ms`).
int udp_ask(uint32_t ip_be, uint16_t port, const uint8_t* q, size_t qlen, uint8_t* out, size_t cap, int ms) {
    const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = ip_be;
    int n = -1;
    if (sendto(fd, q, qlen, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == static_cast<int>(qlen))
        n = static_cast<int>(recv(fd, out, cap, 0));
    close(fd);
    return n;
}

// A DNS name at `at` (with compression), as dotted text.
bool dns_name(const uint8_t* m, int len, int at, std::string& out, int depth = 0) {
    while (at < len && depth < 8) {
        const uint8_t l = m[at];
        if (l == 0) return true;
        if ((l & 0xC0) == 0xC0) {
            if (at + 1 >= len) return false;
            return dns_name(m, len, ((l & 0x3F) << 8) | m[at + 1], out, depth + 1);
        }
        if (at + 1 + l > len) return false;
        if (!out.empty()) out += '.';
        out.append(reinterpret_cast<const char*>(m + at + 1), l);
        at += 1 + l;
    }
    return false;
}
int dns_skip(const uint8_t* m, int len, int at) {
    while (at < len) {
        if (m[at] == 0) return at + 1;
        if ((m[at] & 0xC0) == 0xC0) return at + 2;
        at += 1 + m[at];
    }
    return len;
}

// The name the network's DNS server (usually the router, from the DHCP leases) gives the address.
std::string reverse_dns(uint32_t ip_be, uint32_t dns_be) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&ip_be);
    uint8_t q[64] = {0x4e, 0x44, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};  // id, recursion desired, 1 question
    size_t n = 12;
    char label[4];
    for (int i = 3; i >= 0; --i) {
        const int l = std::snprintf(label, sizeof(label), "%u", b[i]);
        q[n++] = static_cast<uint8_t>(l);
        std::memcpy(q + n, label, l);
        n += l;
    }
    for (const char* part : {"in-addr", "arpa"}) {
        const size_t l = std::strlen(part);
        q[n++] = static_cast<uint8_t>(l);
        std::memcpy(q + n, part, l);
        n += l;
    }
    q[n++] = 0;
    const uint8_t tail[] = {0, 12, 0, 1};  // PTR, IN
    std::memcpy(q + n, tail, 4);
    n += 4;
    uint8_t r[512];
    const int len = udp_ask(dns_be, 53, q, n, r, sizeof(r), 1200);
    if (len < 12 || r[0] != 0x4e || r[1] != 0x44 || (r[3] & 0x0F) != 0) return {};
    const int answers = (r[6] << 8) | r[7];
    int at = dns_skip(r, len, 12) + 4;
    for (int i = 0; i < answers && at + 10 <= len; ++i) {
        at = dns_skip(r, len, at);
        if (at + 10 > len) break;
        const int type = (r[at] << 8) | r[at + 1];
        const int rdlen = (r[at + 8] << 8) | r[at + 9];
        at += 10;
        if (type == 12) {
            std::string name;
            if (dns_name(r, len, at, name) && !name.empty()) return name;
        }
        at += rdlen;
    }
    return {};
}

// The computer's own NetBIOS name (Windows, and Macs with file sharing on): a node status request.
std::string netbios_name(uint32_t ip_be) {
    uint8_t q[50] = {0x4e, 0x42, 0x00, 0x00, 0, 1, 0, 0, 0, 0, 0, 0, 0x20};
    q[13] = 'C';
    q[14] = 'K';  // "*" encoded, then 15 NULs as 'AA'
    for (int i = 15; i < 45; ++i) q[i] = 'A';
    q[45] = 0;
    q[46] = 0;
    q[47] = 0x21;  // NBSTAT
    q[48] = 0;
    q[49] = 1;     // IN
    uint8_t r[600];
    const int len = udp_ask(ip_be, 137, q, sizeof(q), r, sizeof(r), 800);
    if (len < 57 || r[0] != 0x4e || r[1] != 0x42) return {};
    int at = dns_skip(r, len, 12) + 10;  // name, type, class, ttl, rdlength
    if (at >= len) return {};
    const int count = r[at++];
    for (int i = 0; i < count && at + 18 <= len; ++i, at += 18) {
        const uint8_t suffix = r[at + 15];
        const bool group = r[at + 16] & 0x80;
        if (suffix != 0x00 || group) continue;  // the workstation name
        std::string name(reinterpret_cast<const char*>(r + at), 15);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (!name.empty()) return name;
    }
    return {};
}

}  // namespace

Peer describe(const std::string& ip) {
    Peer p;
    esp_ip4_addr_t a{};
    if (esp_netif_str_to_ip4(ip.c_str(), &a) != ESP_OK) return p;
    // The DNS server of the link we are on.
    esp_netif_t* netif = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        netif = g_eth_link && !g_eth_ip.empty() ? g_eth_netif : g_sta_netif;
    }
    esp_netif_dns_info_t dns{};
    if (netif && esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK && dns.ip.type == ESP_IPADDR_TYPE_V4 &&
        dns.ip.u_addr.ip4.addr != 0) {
        std::string name = reverse_dns(a.addr, dns.ip.u_addr.ip4.addr);
        if (!name.empty()) {
            p.hostname = name;
            const size_t dot = name.find('.');  // "MacBookPro.lan" -> "MacBookPro"
            p.name = dot == std::string::npos ? name : name.substr(0, dot);
        }
    }
    if (p.name.empty()) p.name = netbios_name(a.addr);
    return p;
}

int wifi_rssi() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_sta_connected || g_eth_link) return 0;
    }
    wifi_ap_record_t ap{};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

esp_err_t start_mdns(const std::string& name, const std::string& serial, uint16_t port) {
    esp_err_t err = mdns_init();
    if (err != ESP_OK) return err;
    mdns_hostname_set(hostname().c_str());
    mdns_instance_name_set(name.c_str());
    mdns_txt_item_t txt[] = {
        {"dt", "215"},
        {"sn", serial.c_str()},
    };
    return mdns_service_add(name.c_str(), "_elg", "_tcp", port, txt, sizeof(txt) / sizeof(txt[0]));
}

}  // namespace net
