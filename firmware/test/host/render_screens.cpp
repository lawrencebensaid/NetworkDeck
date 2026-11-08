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
        Ui ui(m);
        NetInfo n;
        n.link = "Ethernet";
        n.ip = "10.0.1.187";
        n.firmware = "1.00.000";
        ui.set_net(n);
        save(m, ui, dir + "/" + tag + "-network-dhcp.ppm");
        n.dhcp = false;
        n.ip = "192.168.100.200";
        ui.set_net(n);
        save(m, ui, dir + "/" + tag + "-network-static.ppm");
        n.dhcp = true;
        n.ip = "10.0.1.187";
        ui.set_net(n);
        ui.on_key(m.cols >= 5 ? 3 : 2, true, 0);
        ui.on_key(m.cols >= 5 ? 4 : 1, true, 0);
        save(m, ui, dir + "/" + tag + "-editor.ppm");
        ui.prompt(1, "Join Wi-Fi 'Home'?", 0);
        save(m, ui, dir + "/" + tag + "-prompt.ppm");
        ui.cancel_prompt(1);
        Ui hs(m);
        hs.set_hotspot({true, "Deck32-204C", "abcd2345", "192.168.4.1"});
        save(m, hs, dir + "/" + tag + "-hotspot.ppm");
    }
    return 0;
}
