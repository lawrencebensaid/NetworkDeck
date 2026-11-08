#include "deck_ui/canvas.hpp"

#include <algorithm>
#include <cmath>

extern "C" {
#include "qrcodegen.h"
}

namespace deck_ui {

Canvas::Canvas(int width, int height, Rgb background) : w_(width), h_(height), px_(static_cast<size_t>(width) * height * 3) {
    fill(background);
}

Rgb Canvas::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return {};
    const size_t i = (static_cast<size_t>(y) * w_ + x) * 3;
    return {px_[i], px_[i + 1], px_[i + 2]};
}

void Canvas::set(int x, int y, Rgb c) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    const size_t i = (static_cast<size_t>(y) * w_ + x) * 3;
    px_[i] = c.r;
    px_[i + 1] = c.g;
    px_[i + 2] = c.b;
}

void Canvas::blend(int x, int y, Rgb c, uint8_t a) {
    if (a == 0 || x < 0 || y < 0 || x >= w_ || y >= h_) return;
    if (a == 255) return set(x, y, c);
    const size_t i = (static_cast<size_t>(y) * w_ + x) * 3;
    auto mix = [a](uint8_t bg, uint8_t fg) { return static_cast<uint8_t>((fg * a + bg * (255 - a) + 127) / 255); };
    px_[i] = mix(px_[i], c.r);
    px_[i + 1] = mix(px_[i + 1], c.g);
    px_[i + 2] = mix(px_[i + 2], c.b);
}

void Canvas::fill(Rgb c) { rect(0, 0, w_, h_, c); }

void Canvas::rect(int x, int y, int w, int h, Rgb c) {
    for (int yy = std::max(0, y); yy < std::min(h_, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(w_, x + w); ++xx) set(xx, yy, c);
}

void Canvas::round_rect(int x, int y, int w, int h, int r, Rgb c) {
    r = std::min({r, w / 2, h / 2});
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            // Distance from the nearest corner centre, for pixels in a corner square.
            const float cx = xx < r ? r - 0.5f : (xx >= w - r ? w - r - 0.5f : xx + 0.5f);
            const float cy = yy < r ? r - 0.5f : (yy >= h - r ? h - r - 0.5f : yy + 0.5f);
            const float d = std::hypot(xx + 0.5f - cx, yy + 0.5f - cy);
            const float cover = std::clamp(r - d + 0.5f, 0.0f, 1.0f);
            blend(x + xx, y + yy, c, static_cast<uint8_t>(cover * 255));
        }
    }
}

void Canvas::line(float x0, float y0, float x1, float y1, float t, Rgb c) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = std::max(dx * dx + dy * dy, 1e-6f);
    const int minx = static_cast<int>(std::floor(std::min(x0, x1) - t)), maxx = static_cast<int>(std::ceil(std::max(x0, x1) + t));
    const int miny = static_cast<int>(std::floor(std::min(y0, y1) - t)), maxy = static_cast<int>(std::ceil(std::max(y0, y1) + t));
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float u = std::clamp(((px - x0) * dx + (py - y0) * dy) / len2, 0.0f, 1.0f);
            const float d = std::hypot(px - (x0 + u * dx), py - (y0 + u * dy));
            const float cover = std::clamp(t / 2 - d + 0.5f, 0.0f, 1.0f);
            blend(x, y, c, static_cast<uint8_t>(cover * 255));
        }
    }
}

void Canvas::triangle(float x0, float y0, float x1, float y1, float x2, float y2, Rgb c) {
    auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    const float area = edge(x0, y0, x1, y1, x2, y2);
    if (std::fabs(area) < 1e-6f) return;
    const int minx = static_cast<int>(std::floor(std::min({x0, x1, x2}))), maxx = static_cast<int>(std::ceil(std::max({x0, x1, x2})));
    const int miny = static_cast<int>(std::floor(std::min({y0, y1, y2}))), maxy = static_cast<int>(std::ceil(std::max({y0, y1, y2})));
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            // 4x4 supersampling for smooth edges.
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    const float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                    const float a = edge(x1, y1, x2, y2, px, py), b = edge(x2, y2, x0, y0, px, py), d = edge(x0, y0, x1, y1, px, py);
                    if (area > 0 ? (a >= 0 && b >= 0 && d >= 0) : (a <= 0 && b <= 0 && d <= 0)) ++hits;
                }
            }
            blend(x, y, c, static_cast<uint8_t>(hits * 255 / 16));
        }
    }
}

int Canvas::text_width(const Font& f, const std::string& s) {
    int w = 0;
    for (char ch : s)
        if (const Glyph* g = f.glyph(ch)) w += g->advance;
    return w;
}

void Canvas::text(const Font& f, int x, int y, const std::string& s, Rgb c) {
    for (char ch : s) {
        const Glyph* g = f.glyph(ch);
        if (!g) continue;
        const uint8_t* bits = f.bitmap + g->offset;
        for (int gy = 0; gy < g->h; ++gy)
            for (int gx = 0; gx < g->w; ++gx) blend(x + g->x + gx, y + g->y + gy, c, bits[gy * g->w + gx]);
        x += g->advance;
    }
}

void Canvas::text_centered(const Font& f, int cx, int y, const std::string& s, Rgb c) {
    text(f, cx - text_width(f, s) / 2, y, s, c);
}

const Font& Canvas::fit(const std::string& s, int max_w) {
    for (const Font* f : {&kLargeFont, &kMediumFont, &kSmallFont})
        if (text_width(*f, s) <= max_w) return *f;
    return kSmallFont;
}

bool Canvas::qr(const std::string& s, int x, int y, int size) {
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(s.c_str(), tmp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true))
        return false;
    const int n = qrcodegen_getSize(qr);
    const int quiet = 1;  // the key's black border plus this white margin is enough for phone cameras
    const int scale = size / (n + 2 * quiet);
    if (scale < 1) return false;
    const int total = (n + 2 * quiet) * scale;
    const int ox = x + (size - total) / 2, oy = y + (size - total) / 2;
    rect(ox, oy, total, total, color::kWhite);
    for (int my = 0; my < n; ++my)
        for (int mx = 0; mx < n; ++mx)
            if (qrcodegen_getModule(qr, mx, my)) rect(ox + (mx + quiet) * scale, oy + (my + quiet) * scale, scale, scale, color::kBlack);
    return true;
}

Canvas Canvas::crop(int sx, int sy, int w, int h) const {
    Canvas out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) out.set(x, y, at(sx + x, sy + y));
    return out;
}

}  // namespace deck_ui
