// The dock's own screens, as a pure state machine: key presses and time in, per-key drawings and
// settings changes out. The ESP side (deck_ui.cpp) owns the deck and the USB traffic.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "deck_ui/canvas.hpp"
#include "deck_ui/model.hpp"

namespace deck_ui {

using Ip4 = std::array<uint8_t, 4>;

struct NetInfo {
    std::string link = "No link";  // "Ethernet", "Wi-Fi", "Hotspot", "No link"
    std::string ip;                 // empty while there is no address
    std::string firmware;
    bool dhcp = true;
    Ip4 ip4{192, 168, 1, 50}, mask{255, 255, 255, 0}, gateway{192, 168, 1, 1};  // static settings
};

struct Hotspot {
    bool active = false;
    std::string ssid, password, url;
};

struct UiEvent {
    enum class Kind { None, SetDhcp, SetStatic, PromptAnswer, FactoryReset };
    Kind kind = Kind::None;
    Ip4 ip{}, mask{}, gateway{};
    uint32_t prompt_id = 0;
    bool yes = false;
};

class Ui {
public:
    explicit Ui(const Model& model);

    void set_net(const NetInfo& n);
    void set_hotspot(const Hotspot& h);
    // Shows a yes/no question over everything else; answered by a key press or "no" after 30 s.
    void prompt(uint32_t id, const std::string& question, uint32_t now_ms);
    void cancel_prompt(uint32_t id);
    bool prompting() const { return prompt_.active; }

    UiEvent on_key(int key, bool down, uint32_t now_ms);
    UiEvent on_tick(uint32_t now_ms);  // prompt timeout and ↑/↓ auto-repeat

    // Increments whenever anything visible changes.
    uint32_t version() const { return version_; }
    void render(int key, Canvas& c) const;

    static constexpr uint32_t kPromptTimeoutMs = 30000;
    // Holding the two top corner keys this long asks for a factory reset.
    static constexpr uint32_t kResetHoldMs = 10000;

private:
    enum class Page { Network, Editor };
    enum class Role { Blank, Ip, Link, Firmware, Mode, Field, Octet, Up, Down, Ok, Cancel };
    struct Slot {
        Role role = Role::Blank;
        int arg = 0;  // octet index for Role::Octet
    };

    Slot slot(int key) const;
    void touch() { ++version_; }
    Ip4& field();
    void step(int delta);
    UiEvent press(const Slot& s, uint32_t now);

    const Model& m_;
    NetInfo net_;
    Hotspot hotspot_;
    Page page_ = Page::Network;
    int field_ = 0;   // 0 ip, 1 mask, 2 gateway
    int octet_ = 0;
    Ip4 edit_[3]{};
    struct {
        bool active = false;
        uint32_t id = 0;
        std::string text;
        uint32_t since = 0;
    } prompt_;
    uint64_t down_ = 0;    // keys currently pressed
    uint32_t corners_since_ = 0;
    bool corners_fired_ = false;
    int held_ = -1;        // key held for auto-repeat
    uint32_t held_since_ = 0, last_repeat_ = 0;
    uint32_t version_ = 1;
};

std::string ip_to_string(const Ip4& ip);

}  // namespace deck_ui
