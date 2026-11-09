#include "deck_ui/ui.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace deck_ui {
namespace {

constexpr uint32_t kRepeatDelayMs = 400;
constexpr uint32_t kRepeatEveryMs = 60;
constexpr Rgb kModeKey{76, 60, 236};  // the Elgato dock's lit Mode key, a violet blue

bool parse_ip(const std::string& s, Ip4& out) {
    unsigned a, b, c, d;
    char tail;
    if (std::sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    out = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d)};
    return true;
}

// Greedy word wrap to `max_w`; words wider than a line are split between characters, preferring a
// break after '.', '-' or ':' (addresses, names).
std::vector<std::string> wrap(const std::string& text, const Font& f, int max_w) {
    std::vector<std::string> words, lines;
    std::string w;
    for (char ch : text) {
        if (ch == ' ') {
            if (!w.empty()) words.push_back(w);
            w.clear();
        } else {
            w += ch;
        }
    }
    if (!w.empty()) words.push_back(w);
    std::string line;
    for (std::string word : words) {
        while (Canvas::text_width(f, word) > max_w) {
            size_t cut = 0;
            for (size_t i = 1; i < word.size(); ++i) {
                if (Canvas::text_width(f, word.substr(0, i)) > max_w) break;
                cut = i;
                if (word[i - 1] == '.' || word[i - 1] == '-' || word[i - 1] == ':') cut = i | (1u << 30);
            }
            size_t at = cut & ~(1u << 30);
            // prefer the last punctuation break if there was one in range
            for (size_t i = at; i > 1; --i)
                if (word[i - 1] == '.' || word[i - 1] == '-' || word[i - 1] == ':') { at = i; break; }
            if (at == 0) at = 1;
            if (!line.empty()) { lines.push_back(line); line.clear(); }
            lines.push_back(word.substr(0, at));
            word = word.substr(at);
        }
        const std::string next = line.empty() ? word : line + " " + word;
        if (Canvas::text_width(f, next) <= max_w) {
            line = next;
        } else {
            if (!line.empty()) lines.push_back(line);
            line = word;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::vector<std::string> wrap_value(const std::string& v, const Font& f, int max_w) { return wrap(v, f, max_w); }

void centered_lines(Canvas& c, const std::vector<std::string>& lines, const Font& f, int top, int bottom, Rgb col) {
    const int lh = f.line_height();
    int y = top + (bottom - top - static_cast<int>(lines.size()) * lh) / 2;
    for (const auto& l : lines) {
        c.text_centered(f, c.width() / 2, y, l, col);
        y += lh;
    }
}

void label_value(Canvas& c, const std::string& label, const std::string& value, Rgb value_color) {
    const int w = c.width();
    c.text_centered(kSmallFont, w / 2, w / 9, label, color::kLabel);
    const Font& f = Canvas::fit(value, w - 6);
    const Font& use = &f == &kLargeFont ? kMediumFont : f;  // values stay at most medium
    auto lines = wrap_value(value, use, w - 6);
    if (lines.size() > 2) lines = wrap_value(value, kSmallFont, w - 6);
    centered_lines(c, lines, lines.size() > 2 ? kSmallFont : use, w / 3, w - w / 12, value_color);
}

void pill(Canvas& c, Rgb bg, const std::string& text, const Font& f, Rgb fg) {
    const int w = c.width(), pad = w / 9;
    c.round_rect(pad, pad, w - 2 * pad, w - 2 * pad, w / 6, bg);
    c.text_centered(f, w / 2, (w - f.line_height()) / 2, text, fg);
}

void icon_up(Canvas& c, bool down) {
    const float w = static_cast<float>(c.width());
    pill(c, color::kPanel, "", kSmallFont, color::kWhite);
    const float top = w * 0.34f, bot = w * 0.64f;
    if (down) c.triangle(w * 0.3f, top, w * 0.7f, top, w * 0.5f, bot, color::kWhite);
    else c.triangle(w * 0.3f, bot, w * 0.5f, top, w * 0.7f, bot, color::kWhite);
}

void icon_ok(Canvas& c) {
    const float w = static_cast<float>(c.width());
    pill(c, color::kGreen, "", kSmallFont, color::kWhite);
    c.line(w * 0.30f, w * 0.52f, w * 0.44f, w * 0.66f, w * 0.08f, color::kWhite);
    c.line(w * 0.44f, w * 0.66f, w * 0.71f, w * 0.36f, w * 0.08f, color::kWhite);
}

void icon_cancel(Canvas& c) {
    const float w = static_cast<float>(c.width());
    pill(c, color::kRed, "", kSmallFont, color::kWhite);
    c.line(w * 0.35f, w * 0.35f, w * 0.65f, w * 0.65f, w * 0.08f, color::kWhite);
    c.line(w * 0.65f, w * 0.35f, w * 0.35f, w * 0.65f, w * 0.08f, color::kWhite);
}

}  // namespace

std::pair<std::string, std::string> ip_parts(const std::string& ip) {
    const size_t first = ip.find('.');
    const size_t second = first == std::string::npos ? first : ip.find('.', first + 1);
    if (second == std::string::npos) return {ip, ""};
    return {ip.substr(0, second + 1), ip.substr(second + 1)};
}

std::string ip_to_string(const Ip4& ip) {
    char b[16];
    std::snprintf(b, sizeof(b), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return b;
}

Ui::Ui(const Model& model) : m_(model) {}

void Ui::set_net(const NetInfo& n) {
    net_ = n;
    touch();
}

void Ui::set_hotspot(const Hotspot& h) {
    hotspot_ = h;
    touch();
}

void Ui::prompt(uint32_t id, const std::string& question, uint32_t now_ms) {
    prompt_.active = true;
    prompt_.id = id;
    prompt_.text = question;
    prompt_.since = now_ms;
    held_ = -1;
    touch();
}

void Ui::cancel_prompt(uint32_t id) {
    if (prompt_.active && prompt_.id == id) {
        prompt_.active = false;
        touch();
    }
}

Ip4& Ui::field() { return edit_[field_]; }

// The static editor, pre-filled with the stored static settings and the current address if there is one.
void Ui::open_editor() {
    edit_[0] = net_.ip4;
    edit_[1] = net_.mask;
    edit_[2] = net_.gateway;
    Ip4 now_ip;
    if (parse_ip(net_.ip, now_ip)) edit_[0] = now_ip;
    page_ = Page::Editor;
    field_ = 0;
    octet_ = 0;
}

void Ui::step(int delta) {
    uint8_t& o = field()[octet_];
    o = static_cast<uint8_t>((o + delta + 256) % 256);
    touch();
}

Ui::Slot Ui::slot(int key) const {
    const bool big = m_.cols >= 5;
    if (prompt_.active) {
        if (big) return key == 10 ? Slot{Role::Cancel} : key == 14 ? Slot{Role::Ok} : Slot{};
        return key == 3 ? Slot{Role::Cancel} : key == 5 ? Slot{Role::Ok} : Slot{};
    }
    if (hotspot_.active) return {};
    if (page_ == Page::Network) {
        // As on the Elgato dock: IP over the first row's first three keys, then Firmware, Mode and
        // Setup under them.
        static const Role kRow0[] = {Role::IpLabel, Role::IpHead, Role::IpTail};
        static const Role kRow1[] = {Role::Firmware, Role::Mode, Role::Setup};
        if (key < 3) return {kRow0[key]};
        if (key >= m_.cols && key < m_.cols + 3) return {kRow1[key - m_.cols]};
        return {};
    }
    if (big) {
        if (key == 0) return {Role::Field};
        if (key >= 1 && key <= 4) return {Role::Octet, key - 1};
        if (key == 10) return {Role::Cancel};
        if (key == 12) return {Role::Up};
        if (key == 13) return {Role::Down};
        if (key == 14) return {Role::Ok};
        return {};
    }
    static const Role kMiniEdit[] = {Role::Field, Role::Octet, Role::Up, Role::Cancel, Role::Ok, Role::Down};
    return key < 6 ? Slot{kMiniEdit[key], octet_} : Slot{};
}

UiEvent Ui::press(const Slot& s, uint32_t now) {
    UiEvent ev;
    if (prompt_.active) {
        if (s.role == Role::Ok || s.role == Role::Cancel) {
            ev.kind = UiEvent::Kind::PromptAnswer;
            ev.prompt_id = prompt_.id;
            ev.yes = s.role == Role::Ok;
            prompt_.active = false;
            touch();
        }
        return ev;
    }
    switch (s.role) {
        case Role::Mode:
            // DHCP -> static needs an address: the editor. Static -> DHCP at once.
            if (net_.dhcp) open_editor();
            else ev.kind = UiEvent::Kind::SetDhcp;
            touch();
            break;
        case Role::Setup:
            open_editor();
            touch();
            break;
        case Role::Octet:
            octet_ = m_.cols >= 5 ? s.arg : (octet_ + 1) % 4;
            touch();
            break;
        case Role::Up:
        case Role::Down:
            step(s.role == Role::Up ? 1 : -1);
            held_since_ = last_repeat_ = now;
            break;
        case Role::Ok:
            if (field_ < 2) {
                ++field_;
                octet_ = 0;
            } else {
                ev.kind = UiEvent::Kind::SetStatic;
                ev.ip = edit_[0];
                ev.mask = edit_[1];
                ev.gateway = edit_[2];
                page_ = Page::Network;
            }
            touch();
            break;
        case Role::Cancel:
            page_ = Page::Network;
            touch();
            break;
        default:
            break;
    }
    return ev;
}

UiEvent Ui::on_key(int key, bool down, uint32_t now_ms) {
    const uint64_t corners = (1ull << 0) | (1ull << (m_.cols - 1));
    const bool had_corners = (down_ & corners) == corners;
    if (key >= 0 && key < 64) {
        if (down) down_ |= 1ull << key;
        else down_ &= ~(1ull << key);
    }
    if ((down_ & corners) == corners && !had_corners) {
        corners_since_ = now_ms;
        corners_fired_ = false;
    }
    if (!down) {
        if (key == held_) held_ = -1;
        return {};
    }
    const Slot s = slot(key);
    if (s.role == Role::Up || s.role == Role::Down) held_ = key;
    return press(s, now_ms);
}

UiEvent Ui::on_tick(uint32_t now_ms) {
    UiEvent ev;
    const uint64_t corners = (1ull << 0) | (1ull << (m_.cols - 1));
    if (!prompt_.active && (down_ & corners) == corners && !corners_fired_ && now_ms - corners_since_ >= kResetHoldMs) {
        corners_fired_ = true;
        ev.kind = UiEvent::Kind::FactoryReset;
        return ev;
    }
    if (prompt_.active && now_ms - prompt_.since >= kPromptTimeoutMs) {
        ev.kind = UiEvent::Kind::PromptAnswer;
        ev.prompt_id = prompt_.id;
        ev.yes = false;
        prompt_.active = false;
        touch();
        return ev;
    }
    if (held_ >= 0 && !prompt_.active && now_ms - held_since_ >= kRepeatDelayMs && now_ms - last_repeat_ >= kRepeatEveryMs) {
        const Slot s = slot(held_);
        if (s.role == Role::Up || s.role == Role::Down) {
            step(s.role == Role::Up ? 1 : -1);
            last_repeat_ = now_ms;
        }
    }
    return ev;
}

void Ui::render(int key, Canvas& c) const {
    c.fill(color::kBlack);
    const int k = m_.key_px;
    const bool big = m_.cols >= 5;

    if (prompt_.active) {
        const Slot s = slot(key);
        if (s.role == Role::Ok) return icon_ok(c);
        if (s.role == Role::Cancel) return icon_cancel(c);
        // The question flows key by key over the top row(s), three lines per key, centred on row 0.
        const Font& f = kMediumFont;
        auto lines = wrap(prompt_.text, f, k - 8);
        const int per_key = 3;
        const int chunks = (static_cast<int>(lines.size()) + per_key - 1) / per_key;
        const int text_keys = big ? 10 : 3;
        const int start = chunks <= m_.cols ? (m_.cols - chunks) / 2 : 0;
        const int i = key - start;
        if (key < text_keys && i >= 0 && i < chunks) {
            std::vector<std::string> mine(lines.begin() + i * per_key,
                                          lines.begin() + std::min<int>(static_cast<int>(lines.size()), (i + 1) * per_key));
            centered_lines(c, mine, f, 0, k, color::kWhite);
        }
        return;
    }

    if (hotspot_.active) {
        if (key == 0) {
            c.qr("WIFI:T:WPA;S:" + hotspot_.ssid + ";P:" + hotspot_.password + ";;", 0, 0, k);
            return;
        }
        if (key == 1) return label_value(c, "Wi-Fi", hotspot_.ssid, color::kWhite);
        if (key == 2) return label_value(c, "Password", hotspot_.password, color::kWhite);
        if (key == (big ? 3 : 3)) return label_value(c, "Then open", hotspot_.url, color::kWhite);
        return;
    }

    const Slot s = slot(key);
    switch (s.role) {
        case Role::IpLabel:
            centered_lines(c, {"IP"}, kMonoFont, 0, k, color::kWhite);
            break;
        case Role::IpHead:
        case Role::IpTail: {
            // "GETTING" "..." until there is an address, as the Elgato dock does.
            const auto parts = ip_parts(net_.ip);
            const std::string t = net_.ip.empty() ? (s.role == Role::IpHead ? "GETTING" : "...")
                                                  : (s.role == Role::IpHead ? parts.first : parts.second);
            centered_lines(c, {t}, kMonoFont, 0, k, color::kWhite);
            break;
        }
        case Role::Firmware:
            centered_lines(c, {"Firmware", net_.firmware}, kMonoFont, 0, k, color::kWhite);
            break;
        case Role::Mode: {
            // A lit key: the whole face blue, the mode on it.
            c.round_rect(0, 0, k, k, k / 7, kModeKey);
            const std::string t = net_.dhcp ? "DHCP" : "Static";
            const Font& f = Canvas::text_width(kLargeFont, t) <= k - 8 ? kLargeFont : kMediumFont;
            centered_lines(c, {"Mode:", t}, f, 0, k, color::kWhite);
            break;
        }
        case Role::Setup: {
            // A button: the word, and a bar under it.
            const Font& f = kLargeFont;
            const int y = (k - f.line_height()) / 2 - 2;
            c.text_centered(f, k / 2, y, "Setup", color::kWhite);
            const int bar = k / 3;
            c.round_rect((k - bar) / 2, y + f.line_height() + k / 12, bar, std::max(2, k / 32), 1, color::kLabel);
            break;
        }
        case Role::Field: {
            static const char* kNames[] = {"IP", "Mask", "Gateway"};
            c.text_centered(kSmallFont, k / 2, 8, "Static", color::kLabel);
            c.text_centered(Canvas::fit(kNames[field_], k - 6), k / 2, k / 2 - 12, kNames[field_], color::kWhite);
            char step_txt[32];
            if (big) std::snprintf(step_txt, sizeof(step_txt), "%d / 3", field_ + 1);
            else std::snprintf(step_txt, sizeof(step_txt), "%d/3  .%d", field_ + 1, octet_ + 1);
            c.text_centered(kSmallFont, k / 2, k - 20, step_txt, color::kLabel);
            break;
        }
        case Role::Octet: {
            const int i = big ? s.arg : octet_;
            const bool sel = big ? i == octet_ : true;
            pill(c, sel ? color::kBlue : color::kPanel, std::to_string(edit_[field_][i]), kLargeFont, color::kWhite);
            break;
        }
        case Role::Up:
            icon_up(c, false);
            break;
        case Role::Down:
            icon_up(c, true);
            break;
        case Role::Ok:
            icon_ok(c);
            break;
        case Role::Cancel:
            icon_cancel(c);
            break;
        case Role::Blank:
            break;
    }
}

}  // namespace deck_ui
