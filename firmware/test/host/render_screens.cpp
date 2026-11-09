// Renders every deck_ui screen for the MK.2 and the Mini as PPM images laid out like the real deck,
// so layouts can be checked on the Mac:  render_screens <out-dir>
#include <cstdio>
#include <string>

#include "deck_ui/canvas.hpp"
#include "deck_ui/model.hpp"
#include "deck_ui/ui.hpp"

using namespace deck_ui;

static void save(const Model& m, const Ui& ui, const std::string& path) {
    const int k = m.key_px, gap = k / 4;
    Canvas deck(m.cols * k + (m.cols + 1) * gap, m.rows * k + (m.rows + 1) * gap, {40, 40, 40});
    Canvas key(k, k);
    for (int i = 0; i < m.keys(); ++i) {
        ui.render(i, key);
        const int ox = gap + (i % m.cols) * (k + gap), oy = gap + (i / m.cols) * (k + gap);
        for (int y = 0; y < k; ++y)
            for (int x = 0; x < k; ++x) deck.set(ox + x, oy + y, key.at(x, y));
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", deck.width(), deck.height());
    std::fwrite(deck.rgb().data(), 1, deck.rgb().size(), f);
    std::fclose(f);
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    for (uint16_t pid : {0x0080, 0x0063}) {
        const Model& m = *find_model(pid);
        const std::string tag = pid == 0x0080 ? "mk2" : "mini";
        const int cols = m.cols;
        auto out = [&](const Ui& ui, const char* name) { save(m, ui, dir + "/" + tag + "-" + name + ".ppm"); };
        Ui ui(m);
        NetInfo n;
        n.link = "Ethernet";
        n.ip = "10.0.1.187";
        n.firmware = "1.01.017";
        n.wifi_ssid = "Home";
        ui.set_net(n);
        out(ui, "network-dhcp");
        n.dhcp = false;
        n.ip = "192.168.100.200";
        ui.set_net(n);
        out(ui, "network-static");
        n.dhcp = true;
        n.ip.clear();
        ui.set_net(n);
        out(ui, "network-getting");
        n.ip = "10.0.1.187";
        ui.set_net(n);
        ui.on_key(cols + 2, true, 0);  // Setup
        ui.on_key(cols + 2, false, 0);
        out(ui, "setup");
        ui.on_key(1, true, 0);  // Ethernet
        ui.on_key(1, false, 0);
        out(ui, "ethernet");
        ui.on_key(2, true, 0);  // Static IP
        ui.on_key(2, false, 0);
        out(ui, "editor");
        ui.on_key(cols >= 5 ? 10 : 3, true, 0);  // ✕
        ui.on_key(cols >= 5 ? 10 : 3, false, 0);
        ui.on_key(0, true, 0);  // Back
        ui.on_key(0, false, 0);
        ui.on_key(2, true, 0);  // Wi-Fi
        ui.on_key(2, false, 0);
        out(ui, "wifi-starting");
        ui.set_hotspot({true, "Deck32-204C", "abcd2345", "192.168.4.1"});
        out(ui, "wifi");
        n.wifi_on = false;
        ui.set_hotspot({});
        ui.set_net(n);
        out(ui, "wifi-off");
        ui.prompt(1, "Join Wi-Fi 'Home'?", 0);
        out(ui, "prompt");
        Ui hs(m);
        NetInfo none;
        none.link = "Hotspot";
        none.firmware = "1.01.017";
        hs.set_net(none);
        hs.set_hotspot({true, "Deck32-204C", "abcd2345", "192.168.4.1"});
        out(hs, "hotspot");
    }
    return 0;
}
