#include "net.hpp"

#include <cctype>
#include <cstdio>
#include <mutex>

#include "board.hpp"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "mdns.h"

namespace net {
namespace {

constexpr char TAG[] = "net";
std::mutex g_mutex;
std::string g_ip;
esp_eth_handle_t g_eth = nullptr;

void on_eth_event(void*, esp_event_base_t, int32_t id, void*) {
    switch (id) {
        case ETHERNET_EVENT_CONNECTED: ESP_LOGI(TAG, "Ethernet link up"); break;
        case ETHERNET_EVENT_DISCONNECTED: {
            ESP_LOGW(TAG, "Ethernet link down");
            std::lock_guard<std::mutex> lock(g_mutex);
            g_ip.clear();
            break;
        }
        default: break;
    }
}

void on_got_ip(void*, esp_event_base_t, int32_t, void* data) {
    const auto* ev = static_cast<ip_event_got_ip_t*>(data);
    char buf[16];
    std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ev->ip_info.ip));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_ip = buf;
    }
    ESP_LOGI(TAG, "IP %s", buf);
}

}  // namespace

esp_err_t start_ethernet() {
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

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
    err = esp_eth_driver_install(&config, &g_eth);
    if (err != ESP_OK) return err;

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t* netif = esp_netif_new(&netif_cfg);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(g_eth)));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, nullptr));
    return esp_eth_start(g_eth);
}

std::array<uint8_t, 6> mac() {
    std::array<uint8_t, 6> m{};
    esp_read_mac(m.data(), ESP_MAC_ETH);
    return m;
}

std::string ip() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_ip;
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
