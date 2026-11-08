#include <doctest/doctest.h>

#include "deck_ui/canvas.hpp"
#include "deck_ui/model.hpp"
#include "deck_ui/ui.hpp"

using namespace deck_ui;

namespace {

const Model& mk2() { return *find_model(0x0080); }
const Model& mini() { return *find_model(0x0063); }

NetInfo dhcp_net() {
    NetInfo n;
    n.link = "Ethernet";
    n.ip = "10.0.1.187";
    n.firmware = "1.00.000";
    n.dhcp = true;
    return n;
}

}  // namespace

TEST_CASE("model table: MK.2 and Mini") {
    CHECK(find_model(0x0080)->keys() == 15);
    CHECK(find_model(0x00a5) == find_model(0x0080));
    CHECK(find_model(0x0063)->keys() == 6);
    CHECK(find_model(0x0090) == find_model(0x0063));
    CHECK(find_model(0x1234) == nullptr);
}

TEST_CASE("gen2 transform rotates 180 degrees, keeps RGB") {
    Canvas c(72, 72);
    c.set(0, 0, {10, 20, 30});
    auto px = transform(mk2(), c);
    REQUIRE(px.size() == 72 * 72 * 3);
    const size_t last = (71 * 72 + 71) * 3;  // top-left lands bottom-right
    CHECK(px[last] == 10);
    CHECK(px[last + 1] == 20);
    CHECK(px[last + 2] == 30);
}

TEST_CASE("mini transform swaps axes with a y flip and writes BGR") {
    Canvas c(80, 80);
    c.set(5, 0, {10, 20, 30});  // source (x=5, y=0)
    auto px = transform(mini(), c);
    // target (x, y) reads source (y2, x2) with y2 = 79 - y: source x=5,y=0 -> target x=0, y=74
    const size_t at = (74 * 80 + 0) * 3;
    CHECK(px[at] == 30);
    CHECK(px[at + 1] == 20);
    CHECK(px[at + 2] == 10);
}

TEST_CASE("bmp header") {
    std::vector<uint8_t> px(80 * 80 * 3, 0);
    auto b = bmp(mini(), px);
    REQUIRE(b.size() == 54 + px.size());
    CHECK(b[0] == 'B');
    CHECK(b[1] == 'M');
    CHECK(b[10] == 54);
    CHECK(b[18] == 80);
    CHECK(b[28] == 24);
    CHECK((b[38] | (b[39] << 8)) == 2835);
}

TEST_CASE("gen2 image reports: 8-byte header, 1016 bytes each, last flag") {
    std::vector<uint8_t> img(2500, 0xAB);
    auto r = image_reports(mk2(), 7, img);
    REQUIRE(r.size() == 3);
    CHECK(r[0].size() == 1024);
    CHECK(r[0][0] == 0x02);
    CHECK(r[0][1] == 0x07);
    CHECK(r[0][2] == 7);
    CHECK(r[0][3] == 0);
    CHECK((r[0][4] | (r[0][5] << 8)) == 1016);
    CHECK((r[2][6] | (r[2][7] << 8)) == 2);
    CHECK(r[2][3] == 1);
    CHECK((r[2][4] | (r[2][5] << 8)) == 2500 - 2 * 1016);
}

TEST_CASE("gen1 image reports: 16-byte header, key + 1") {
    std::vector<uint8_t> img(2000, 1);
    auto r = image_reports(mini(), 2, img);
    REQUIRE(r.size() == 2);
    CHECK(r[0][1] == 0x01);
    CHECK(r[0][2] == 0);
    CHECK(r[0][4] == 0);
    CHECK(r[0][5] == 3);
    CHECK(r[1][2] == 1);
    CHECK(r[1][4] == 1);
}

TEST_CASE("key states from input reports") {
    uint8_t gen2[64] = {0x01, 0x00, 0x0f, 0x00};
    gen2[4 + 3] = 1;
    auto k = key_states(mk2(), gen2, sizeof(gen2));
    REQUIRE(k.size() == 15);
    CHECK(k[3]);
    CHECK_FALSE(k[0]);
    uint8_t gen1[17] = {0x01};
    gen1[1 + 5] = 1;
    auto m = key_states(mini(), gen1, sizeof(gen1));
    REQUIRE(m.size() == 6);
    CHECK(m[5]);
    uint8_t other[4] = {0x03, 0, 0, 0};
    CHECK(key_states(mk2(), other, 4).empty());
}

TEST_CASE("canvas text draws something and measures width") {
    Canvas c(72, 72);
    CHECK(Canvas::text_width(kMediumFont, "DHCP") > 20);
    c.text(kMediumFont, 2, 2, "DHCP", color::kWhite);
    int lit = 0;
    for (int y = 0; y < 72; ++y)
        for (int x = 0; x < 72; ++x) lit += c.at(x, y).r > 128;
    CHECK(lit > 30);
}

TEST_CASE("qr code of the hotspot string fits on one key") {
    Canvas c(72, 72);
    CHECK(c.qr("WIFI:T:WPA;S:Deck32-204C;P:abcd2345;;", 0, 0, 72));
}

TEST_CASE("static editor: DHCP key opens the editor pre-filled, OK walks the fields, then SetStatic") {
    Ui ui(mk2());
    ui.set_net(dhcp_net());
    CHECK(ui.on_key(3, true, 0).kind == UiEvent::Kind::None);  // mode key -> editor
    // ip pre-filled from the current address; select the last octet and bump it twice
    ui.on_key(4, true, 10);
    ui.on_key(12, true, 20);
    ui.on_key(12, false, 30);
    ui.on_key(12, true, 40);
    ui.on_key(12, false, 50);
    CHECK(ui.on_key(14, true, 60).kind == UiEvent::Kind::None);  // -> mask
    CHECK(ui.on_key(14, true, 70).kind == UiEvent::Kind::None);  // -> gateway
    auto ev = ui.on_key(14, true, 80);
    REQUIRE(ev.kind == UiEvent::Kind::SetStatic);
    CHECK(ev.ip == Ip4{10, 0, 1, 189});
    CHECK(ev.mask == Ip4{255, 255, 255, 0});
}

TEST_CASE("static editor: down wraps, cancel returns without an event") {
    Ui ui(mk2());
    NetInfo n = dhcp_net();
    n.ip = "";
    n.ip4 = {0, 0, 0, 0};
    ui.set_net(n);
    ui.on_key(3, true, 0);
    ui.on_key(13, true, 10);  // first octet 0 -> 255
    ui.on_key(13, false, 20);
    CHECK(ui.on_key(10, true, 30).kind == UiEvent::Kind::None);
    // back on the network page: the mode key opens the editor again rather than doing anything else
    CHECK(ui.on_key(3, true, 40).kind == UiEvent::Kind::None);
}

TEST_CASE("holding up auto-repeats") {
    Ui ui(mk2());
    NetInfo n = dhcp_net();
    n.ip = "10.0.1.0";
    ui.set_net(n);
    ui.on_key(3, true, 0);
    ui.on_key(4, true, 0);    // last octet = 0
    ui.on_key(12, true, 0);   // 1
    for (uint32_t t = 0; t <= 1000; t += 10) ui.on_tick(t);
    ui.on_key(12, false, 1000);
    for (int i = 0; i < 2; ++i) ui.on_key(14, true, 1100);
    auto ev = ui.on_key(14, true, 1200);
    REQUIRE(ev.kind == UiEvent::Kind::SetStatic);
    // the press itself, then one step every 60 ms from 400 ms to 1000 ms (11 steps)
    CHECK(ev.ip[3] == 12);
}

TEST_CASE("mode key in static mode switches back to DHCP") {
    Ui ui(mk2());
    NetInfo n = dhcp_net();
    n.dhcp = false;
    ui.set_net(n);
    CHECK(ui.on_key(3, true, 0).kind == UiEvent::Kind::SetDhcp);
}

TEST_CASE("prompt: OK answers yes, timeout answers no, other keys are ignored") {
    Ui ui(mk2());
    ui.set_net(dhcp_net());
    ui.prompt(7, "Join Wi-Fi 'Home'?", 1000);
    CHECK(ui.prompting());
    CHECK(ui.on_key(3, true, 1100).kind == UiEvent::Kind::None);
    auto yes = ui.on_key(14, true, 1200);
    CHECK(yes.kind == UiEvent::Kind::PromptAnswer);
    CHECK(yes.prompt_id == 7);
    CHECK(yes.yes);
    CHECK_FALSE(ui.prompting());

    ui.prompt(8, "Install firmware?", 2000);
    CHECK(ui.on_tick(2000 + Ui::kPromptTimeoutMs - 1).kind == UiEvent::Kind::None);
    auto no = ui.on_tick(2000 + Ui::kPromptTimeoutMs);
    CHECK(no.kind == UiEvent::Kind::PromptAnswer);
    CHECK_FALSE(no.yes);
}

TEST_CASE("mini prompt keys and editor octet cycling") {
    Ui ui(mini());
    ui.set_net(dhcp_net());
    ui.prompt(1, "Install firmware?", 0);
    CHECK(ui.on_key(5, true, 10).yes);
    ui.on_key(2, true, 20);  // mode key on the Mini -> editor
    ui.on_key(1, true, 30);  // octet key: next octet (1)
    ui.on_key(2, true, 40);  // up: 0 -> 1 for octet 1 of 10.0.1.187
    ui.on_key(2, false, 50);
    ui.on_key(4, true, 60);
    ui.on_key(4, true, 70);
    auto ev = ui.on_key(4, true, 80);
    REQUIRE(ev.kind == UiEvent::Kind::SetStatic);
    CHECK(ev.ip == Ip4{10, 1, 1, 187});
}

TEST_CASE("every screen renders on every key without going out of bounds") {
    for (const Model* m : {&mk2(), &mini()}) {
        Ui ui(*m);
        ui.set_net(dhcp_net());
        Canvas c(m->key_px, m->key_px);
        for (int k = 0; k < m->keys(); ++k) ui.render(k, c);
        ui.on_key(m->cols >= 5 ? 3 : 2, true, 0);
        for (int k = 0; k < m->keys(); ++k) ui.render(k, c);
        ui.prompt(1, "Install firmware 1.01.000?", 0);
        for (int k = 0; k < m->keys(); ++k) ui.render(k, c);
        Hotspot h{true, "Deck32-204C", "abcd2345", "192.168.4.1"};
        ui.cancel_prompt(1);
        ui.set_hotspot(h);
        for (int k = 0; k < m->keys(); ++k) ui.render(k, c);
        CHECK(c.width() == m->key_px);
    }
}

TEST_CASE("holding both top corner keys for 10 s asks for a factory reset, once") {
    Ui ui(mk2());
    ui.set_net(dhcp_net());
    ui.on_key(0, true, 1000);
    ui.on_key(4, true, 1000);
    CHECK(ui.on_tick(1000 + Ui::kResetHoldMs - 1).kind == UiEvent::Kind::None);
    CHECK(ui.on_tick(1000 + Ui::kResetHoldMs).kind == UiEvent::Kind::FactoryReset);
    CHECK(ui.on_tick(1000 + Ui::kResetHoldMs + 100).kind == UiEvent::Kind::None);
    ui.on_key(4, false, 20000);
    CHECK(ui.on_tick(30000).kind == UiEvent::Kind::None);
}
