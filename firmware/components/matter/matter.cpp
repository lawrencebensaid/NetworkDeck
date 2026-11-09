// Matter: see matter.hpp.

#include "matter.hpp"

#include <array>
#include <cstring>
#include <mutex>

#include <app/server/Server.h>
#include <crypto/CHIPCryptoPAL.h>
#include <esp_matter.h>
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

#include "esp_log.h"
#include "eventlog.hpp"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"

namespace matter {
namespace {

constexpr char TAG[] = "matter";
constexpr char kNvsNamespace[] = "nd_matter";
constexpr uint32_t kIterations = 1000;     // PBKDF2 iterations of the SPAKE2+ verifier (the spec minimum)
constexpr uint64_t kLongPressUs = 600000;  // held this long: LongPress instead of ShortRelease
constexpr unsigned kWindowSeconds = 15 * 60;
constexpr unsigned kMaxKeys = 32;          // Stream Deck XL

using namespace esp_matter;
using chip::DeviceLayer::PlatformMgr;

// ── setup code ───────────────────────────────────────────────────────────────
// A random passcode and discriminator per dock (the SDK's example provider would give every dock
// 20202021 / 3840). The verifier is derived from them when the stack first asks for it.
class Commissionable final : public chip::DeviceLayer::CommissionableDataProvider {
public:
    esp_err_t load() {
        nvs_handle_t h;
        esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
        if (err != ESP_OK) return err;
        size_t salt_len = sizeof(salt_);
        const bool have = nvs_get_u32(h, "pin", &passcode_) == ESP_OK && nvs_get_u16(h, "disc", &discriminator_) == ESP_OK &&
                          nvs_get_blob(h, "salt", salt_, &salt_len) == ESP_OK && salt_len == sizeof(salt_) &&
                          chip::SetupPayload::IsValidSetupPIN(passcode_);
        if (!have) {
            do {
                passcode_ = esp_random() % 99999998u + 1u;
            } while (!chip::SetupPayload::IsValidSetupPIN(passcode_));
            discriminator_ = static_cast<uint16_t>(esp_random() & 0x0FFF);
            esp_fill_random(salt_, sizeof(salt_));
            nvs_set_u32(h, "pin", passcode_);
            nvs_set_u16(h, "disc", discriminator_);
            nvs_set_blob(h, "salt", salt_, sizeof(salt_));
            err = nvs_commit(h);
            ESP_LOGI(TAG, "new setup code made");
        }
        nvs_close(h);
        return err;
    }

    uint32_t passcode() const { return passcode_; }
    uint16_t discriminator() const { return discriminator_; }

    CHIP_ERROR GetSetupDiscriminator(uint16_t& d) override {
        d = discriminator_;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR SetSetupDiscriminator(uint16_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }
    CHIP_ERROR GetSpake2pIterationCount(uint32_t& n) override {
        n = kIterations;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan& out) override { return chip::CopySpanToMutableSpan(chip::ByteSpan(salt_), out); }
    CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan& out, size_t& len) override {
        if (!have_verifier_) {
            chip::Crypto::Spake2pVerifier v;
            ReturnErrorOnFailure(v.Generate(kIterations, chip::ByteSpan(salt_), passcode_));
            chip::MutableByteSpan s(verifier_);
            ReturnErrorOnFailure(v.Serialize(s));
            have_verifier_ = true;
        }
        len = sizeof(verifier_);
        return chip::CopySpanToMutableSpan(chip::ByteSpan(verifier_), out);
    }
    CHIP_ERROR GetSetupPasscode(uint32_t& p) override {
        p = passcode_;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR SetSetupPasscode(uint32_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

private:
    uint32_t passcode_ = 0;
    uint16_t discriminator_ = 0;
    uint8_t salt_[16]{};
    uint8_t verifier_[chip::Crypto::kSpake2p_VerifierSerialized_Length]{};
    bool have_verifier_ = false;
};

Commissionable g_commissionable;
std::string g_qr, g_manual;
bool g_running = false;

// ── keys ─────────────────────────────────────────────────────────────────────

struct Key {
    uint16_t endpoint = 0;
    bool down = false;
    bool long_sent = false;
    esp_timer_handle_t timer = nullptr;
};

std::mutex g_mutex;  // g_keys; the timers and key() run on other tasks than the stack
std::array<Key, kMaxKeys> g_keys;
unsigned g_count = 0;

enum class Ev : uint8_t { Initial, Long, ShortRelease, LongRelease };

// Runs on the Matter task. The arg packs the endpoint and the event.
void send(intptr_t arg) {
    const auto endpoint = static_cast<uint16_t>(arg >> 8);
    const auto ev = static_cast<Ev>(arg & 0xff);
    namespace sw = cluster::switch_cluster::event;
    switch (ev) {
        case Ev::Initial: sw::send_initial_press(endpoint, 1); break;
        case Ev::Long: sw::send_long_press(endpoint, 1); break;
        case Ev::ShortRelease: sw::send_short_release(endpoint, 1); break;
        case Ev::LongRelease: sw::send_long_release(endpoint, 1); break;
    }
    esp_matter_attr_val_t position = esp_matter_uint8(ev == Ev::Initial || ev == Ev::Long ? 1 : 0);
    attribute::update(endpoint, chip::app::Clusters::Switch::Id, chip::app::Clusters::Switch::Attributes::CurrentPosition::Id,
                      &position);
}

void schedule(uint16_t endpoint, Ev ev) {
    if (PlatformMgr().ScheduleWork(send, static_cast<intptr_t>(endpoint) << 8 | static_cast<intptr_t>(ev)) != CHIP_NO_ERROR)
        ESP_LOGW(TAG, "key event dropped");
}

void on_long_press(void* arg) {
    const auto index = reinterpret_cast<uintptr_t>(arg);
    std::lock_guard<std::mutex> lock(g_mutex);
    Key& k = g_keys[index];
    if (!k.down || k.long_sent) return;
    k.long_sent = true;
    schedule(k.endpoint, Ev::Long);
}

// ── stack events ─────────────────────────────────────────────────────────────

void on_event(const ChipDeviceEvent* event, intptr_t) {
    using namespace chip::DeviceLayer::DeviceEventType;
    switch (event->Type) {
        case kCommissioningComplete:
            ESP_LOGI(TAG, "added to a controller");
            eventlog::add(eventlog::Cat::Matter, eventlog::Level::Info, "Added to a Matter controller");
            break;
        case kFailSafeTimerExpired:
            ESP_LOGW(TAG, "adding to a controller failed (fail-safe expired)");
            eventlog::add(eventlog::Cat::Matter, eventlog::Level::Warn, "Adding to a Matter controller failed");
            break;
        case kCommissioningWindowOpened:
            ESP_LOGI(TAG, "pairing open");
            break;
        case kCommissioningWindowClosed:
            ESP_LOGI(TAG, "pairing closed");
            break;
        default:
            break;
    }
}

esp_err_t on_attribute(attribute::callback_type_t, uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t*, void*) {
    return ESP_OK;  // nothing on the dock is controlled from Matter
}

esp_err_t on_identify(identification::callback_type_t, uint16_t, uint8_t, uint8_t, void*) { return ESP_OK; }

void make_codes() {
    chip::SetupPayload p;
    p.version = 0;
    p.vendorID = CONFIG_DEVICE_VENDOR_ID;
    p.productID = CONFIG_DEVICE_PRODUCT_ID;
    p.commissioningFlow = chip::CommissioningFlow::kStandard;
    p.rendezvousInformation.SetValue(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork));
    p.discriminator.SetLongValue(g_commissionable.discriminator());
    p.setUpPINCode = g_commissionable.passcode();
    if (chip::QRCodeSetupPayloadGenerator(p).payloadBase38Representation(g_qr) != CHIP_NO_ERROR) g_qr.clear();
    if (chip::ManualSetupPayloadGenerator(p).payloadDecimalStringRepresentation(g_manual) != CHIP_NO_ERROR) g_manual.clear();
}

// The stack lock, waiting at most `ms`: callers are web requests, which must not hang on a busy stack.
bool lock_stack(uint32_t ms) {
    for (uint32_t waited = 0;; waited += 10) {
        if (PlatformMgr().TryLockChipStack()) return true;
        if (waited >= ms) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void copy(char* dst, size_t size, const std::string& s) {
    std::strncpy(dst, s.c_str(), size - 1);
    dst[size - 1] = '\0';
}

}  // namespace

void remember_keys(unsigned keys) {
    nvs_handle_t h;
    if (keys == 0 || keys > kMaxKeys || nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t stored = 0;
    if (nvs_get_u8(h, "keys", &stored) != ESP_OK || stored != keys) {
        nvs_set_u8(h, "keys", static_cast<uint8_t>(keys));
        nvs_commit(h);
        if (g_running) ESP_LOGI(TAG, "a deck with %u keys: Matter shows them after a restart", keys);
    }
    nvs_close(h);
}

esp_err_t start(unsigned default_keys, const std::string& node_label) {
    if (g_running) return ESP_ERR_INVALID_STATE;
    unsigned keys = default_keys;
    {
        nvs_handle_t h;
        uint8_t stored = 0;
        if (nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_u8(h, "keys", &stored) == ESP_OK && stored > 0) keys = stored;
            nvs_close(h);
        }
    }
    if (keys > kMaxKeys) keys = kMaxKeys;

    esp_err_t err = g_commissionable.load();
    if (err != ESP_OK) return err;
    set_custom_commissionable_data_provider(&g_commissionable);
    make_codes();

    node::config_t node_config;
    auto& basic = node_config.root_node.basic_information;
    copy(basic.node_label, sizeof(basic.node_label), node_label);
    node_t* node = node::create(&node_config, on_attribute, on_identify);
    if (!node) return ESP_FAIL;

    for (unsigned i = 0; i < keys; ++i) {
        endpoint::generic_switch::config_t cfg;
        cfg.switch_cluster.number_of_positions = 2;
        cfg.switch_cluster.feature_flags = cluster::switch_cluster::feature::momentary_switch::get_id() |
                                           cluster::switch_cluster::feature::momentary_switch_release::get_id() |
                                           cluster::switch_cluster::feature::momentary_switch_long_press::get_id();
        endpoint_t* ep = endpoint::generic_switch::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
        if (!ep) return ESP_FAIL;
        Key& k = g_keys[i];
        k.endpoint = endpoint::get_id(ep);
        esp_timer_create_args_t t{};
        t.callback = on_long_press;
        t.arg = reinterpret_cast<void*>(static_cast<uintptr_t>(i));
        t.name = "matter_key";
        esp_timer_create(&t, &k.timer);
    }

    err = esp_matter::start(on_event);
    if (err != ESP_OK) return err;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_count = keys;
    }
    g_running = true;
    ESP_LOGI(TAG, "started: %u keys, pairing code %s", keys, g_manual.c_str());
    return ESP_OK;
}

void key(unsigned index, bool down) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (index >= g_count) return;
    Key& k = g_keys[index];
    if (down == k.down) return;
    k.down = down;
    if (down) {
        k.long_sent = false;
        schedule(k.endpoint, Ev::Initial);
        esp_timer_start_once(k.timer, kLongPressUs);
    } else {
        esp_timer_stop(k.timer);
        schedule(k.endpoint, k.long_sent ? Ev::LongRelease : Ev::ShortRelease);
    }
}

Status status() {
    Status s;
    s.running = g_running;
    s.qr = g_qr;
    s.manual = g_manual;
    if (!g_running) return s;
    if (!lock_stack(500)) return s;
    auto& server = chip::Server::GetInstance();
    s.fabrics = server.GetFabricTable().FabricCount();
    s.window_open = server.GetCommissioningWindowManager().IsCommissioningWindowOpen();
    s.window_seconds = kWindowSeconds;
    PlatformMgr().UnlockChipStack();
    return s;
}

esp_err_t open_window() {
    if (!g_running) return ESP_ERR_INVALID_STATE;
    if (!lock_stack(2000)) return ESP_ERR_TIMEOUT;
    auto& windows = chip::Server::GetInstance().GetCommissioningWindowManager();
    const bool ok = windows.IsCommissioningWindowOpen() ||
                    windows.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(kWindowSeconds)) == CHIP_NO_ERROR;
    PlatformMgr().UnlockChipStack();
    return ok ? ESP_OK : ESP_FAIL;
}

bool available() { return true; }

bool has_setup_code() {
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    uint32_t pin = 0;
    const bool have = nvs_get_u32(h, "pin", &pin) == ESP_OK;
    nvs_close(h);
    return have;
}

void erase_namespace(const char* ns) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
}

esp_err_t unpair() {
    erase_namespace(kNvsNamespace);  // a new code the next time it is set up
    if (g_running) {
        // Tell the controllers the dock is leaving, if the stack answers. Not through the SDK's
        // factory reset: that goes through the stack's own task and can stall there.
        if (lock_stack(3000)) {
            chip::Server::GetInstance().GetFabricTable().DeleteAllFabrics();
            PlatformMgr().UnlockChipStack();
            vTaskDelay(pdMS_TO_TICKS(1000));  // the Leave events go out
        } else {
            ESP_LOGW(TAG, "stack busy: leaving without telling the controllers");
        }
    }
    // Everything Matter keeps: the data model, the stack's config, counters and fabrics.
    for (const char* ns : {"esp_matter_kvs", "chip-config", "chip-counters", "CHIP_KVS"}) erase_namespace(ns);
    ESP_LOGI(TAG, "Matter storage erased");
    return ESP_OK;
}

}  // namespace matter
