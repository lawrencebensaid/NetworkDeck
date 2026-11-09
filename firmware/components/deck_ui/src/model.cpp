#include "deck_ui/model.hpp"

#include <algorithm>

namespace deck_ui {
namespace {

constexpr size_t kReport = 1024;

// Stream Deck MK.2 / MK.2 Scissor / Stream Deck (2019): 5x3, 72 px JPEG, rotated 180 degrees.
const Model kGen2Fifteen{"Stream Deck MK.2", 5, 3, 72, Model::Format::Jpeg, Model::Header::Gen2,
                         true, true, false, false, 3, 0};
// Stream Deck Mini (all revisions): 3x2, 80 px BMP, BGR, rotated 90 degrees and flipped.
const Model kMini{"Stream Deck Mini", 3, 2, 80, Model::Format::Bmp, Model::Header::Gen1,
                  false, true, true, true, 0, 2835};

void put_u16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}
void put_u32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

}  // namespace

const Model* find_model(uint16_t pid) {
    switch (pid) {
        case 0x0080:  // MK.2
        case 0x00a5:  // MK.2 Scissor
        case 0x006d:  // Stream Deck (2019)
            return &kGen2Fifteen;
        case 0x0063:  // Mini
        case 0x0090:  // Mini (2019)
        case 0x00b3:  // Mini Discord Edition
            return &kMini;
        default:
            return nullptr;
    }
}

std::vector<uint8_t> transform(const Model& m, const Canvas& key) {
    // Same mapping as transformImageBuffer() in node-elgato-stream-deck: flips first, then the x/y swap.
    const int w = m.rotate ? key.height() : key.width();
    const int h = m.rotate ? key.width() : key.height();
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int x2 = m.x_flip ? w - x - 1 : x;
            int y2 = m.y_flip ? h - y - 1 : y;
            if (m.rotate) std::swap(x2, y2);
            const Rgb c = key.at(x2, y2);
            uint8_t* p = &out[(static_cast<size_t>(y) * w + x) * 3];
            p[0] = m.bgr ? c.b : c.r;
            p[1] = c.g;
            p[2] = m.bgr ? c.r : c.b;
        }
    }
    return out;
}

std::vector<uint8_t> bmp(const Model& m, const std::vector<uint8_t>& pixels) {
    std::vector<uint8_t> b(54 + pixels.size());
    b[0] = 'B';
    b[1] = 'M';
    put_u32(b, 2, static_cast<uint32_t>(pixels.size() + 54));
    put_u32(b, 10, 54);
    put_u32(b, 14, 40);
    put_u32(b, 18, static_cast<uint32_t>(m.key_px));
    put_u32(b, 22, static_cast<uint32_t>(m.key_px));
    put_u16(b, 26, 1);
    put_u16(b, 28, 24);
    put_u32(b, 34, static_cast<uint32_t>(pixels.size()));
    put_u32(b, 38, static_cast<uint32_t>(m.bmp_ppm));
    put_u32(b, 42, static_cast<uint32_t>(m.bmp_ppm));
    std::copy(pixels.begin(), pixels.end(), b.begin() + 54);
    return b;
}

std::vector<std::vector<uint8_t>> image_reports(const Model& m, int key, const std::vector<uint8_t>& image) {
    const size_t header = m.header == Model::Header::Gen2 ? 8 : 16;
    const size_t payload = kReport - header;
    std::vector<std::vector<uint8_t>> out;
    size_t remaining = image.size();
    for (int part = 0; remaining > 0; ++part) {
        std::vector<uint8_t> r(kReport, 0);
        const size_t n = std::min(remaining, payload);
        const bool last = remaining <= payload;
        r[0] = 0x02;
        if (m.header == Model::Header::Gen2) {
            r[1] = 0x07;
            r[2] = static_cast<uint8_t>(key);
            r[3] = last ? 1 : 0;
            put_u16(r, 4, static_cast<uint16_t>(n));
            put_u16(r, 6, static_cast<uint16_t>(part));
        } else {
            r[1] = 0x01;
            r[2] = static_cast<uint8_t>(part);
            r[4] = last ? 1 : 0;
            r[5] = static_cast<uint8_t>(key + 1);
        }
        const size_t off = image.size() - remaining;
        std::copy(image.begin() + static_cast<long>(off), image.begin() + static_cast<long>(off + n), r.begin() + static_cast<long>(header));
        remaining -= n;
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<uint8_t> brightness_report(const Model& m, int percent) {
    percent = std::clamp(percent, 0, 100);
    if (m.header == Model::Header::Gen2) {
        std::vector<uint8_t> r(32, 0);
        r[0] = 0x03;
        r[1] = 0x08;
        r[2] = static_cast<uint8_t>(percent);
        return r;
    }
    std::vector<uint8_t> r(17, 0);
    r[0] = 0x05;
    r[1] = 0x55;
    r[2] = 0xaa;
    r[3] = 0xd1;
    r[4] = 0x01;
    r[5] = static_cast<uint8_t>(percent);
    return r;
}

std::vector<bool> key_states(const Model& m, const uint8_t* report, size_t len) {
    std::vector<bool> keys;
    if (len < 1 || report[0] != 0x01) return keys;
    const size_t start = 1 + static_cast<size_t>(m.key_data_offset);
    if (len < start + static_cast<size_t>(m.keys())) return keys;
    for (int i = 0; i < m.keys(); ++i) keys.push_back(report[start + i] != 0);
    return keys;
}

bool parse_image_report(const Model& m, const uint8_t* r, size_t len, ImagePart& out) {
    if (len < 16 || r[0] != 0x02) return false;
    if (m.header == Model::Header::Gen2) {
        if (r[1] != 0x07) return false;
        out.key = r[2];
        out.last = r[3] != 0;
        out.len = std::min<size_t>(r[4] | (r[5] << 8), len - 8);
        out.part = r[6] | (r[7] << 8);
        out.data = r + 8;
    } else {
        if (r[1] != 0x01 || r[5] == 0) return false;
        out.part = r[2];
        out.last = r[4] != 0;
        out.key = r[5] - 1;
        out.data = r + 16;
        out.len = len - 16;  // gen1 has no length field: the BMP header says how much is real
    }
    return out.key >= 0 && out.key < m.keys();
}

bool parse_fill_report(const Model& m, const uint8_t* r, size_t len, int& key, Rgb& color) {
    if (m.header != Model::Header::Gen2 || len < 6 || r[0] != 0x03 || r[1] != 0x06) return false;
    key = r[2];
    color = {r[3], r[4], r[5]};
    return key < m.keys();
}

const char* css_transform(const Model& m) {
    // Inverse of transform(): flips undo themselves; the Mini's swap + y flip, shown bottom-up as BMP
    // rows, comes out as a transpose.
    if (m.rotate) return "rotate(90deg) scaleX(-1)";
    if (m.x_flip && m.y_flip) return "rotate(180deg)";
    if (m.x_flip) return "scaleX(-1)";
    if (m.y_flip) return "scaleY(-1)";
    return "none";
}

}  // namespace deck_ui
