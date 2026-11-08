// An RGB888 drawing surface for one key (or a banner spanning several keys).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "deck_ui/font.hpp"

namespace deck_ui {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

namespace color {
inline constexpr Rgb kBlack{0, 0, 0};
inline constexpr Rgb kWhite{255, 255, 255};
inline constexpr Rgb kLabel{142, 142, 147};    // secondary text
inline constexpr Rgb kPanel{44, 44, 46};       // key background for controls
inline constexpr Rgb kBlue{10, 132, 255};      // DHCP, selection
inline constexpr Rgb kGreen{48, 209, 88};      // confirm
inline constexpr Rgb kRed{255, 69, 58};        // cancel
inline constexpr Rgb kOrange{255, 159, 10};    // static mode
}  // namespace color

class Canvas {
public:
    Canvas(int width, int height, Rgb background = color::kBlack);

    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<uint8_t>& rgb() const { return px_; }  // row-major, 3 bytes per pixel

    Rgb at(int x, int y) const;
    void set(int x, int y, Rgb c);
    void blend(int x, int y, Rgb c, uint8_t alpha);

    void fill(Rgb c);
    void rect(int x, int y, int w, int h, Rgb c);
    void round_rect(int x, int y, int w, int h, int radius, Rgb c);
    void line(float x0, float y0, float x1, float y1, float thickness, Rgb c);  // anti-aliased
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Rgb c);

    static int text_width(const Font& f, const std::string& s);
    // Draws with the top of the line at y.
    void text(const Font& f, int x, int y, const std::string& s, Rgb c);
    void text_centered(const Font& f, int cx, int y, const std::string& s, Rgb c);
    // Largest of the given fonts that fits `max_w`, smallest otherwise.
    static const Font& fit(const std::string& s, int max_w);

    // QR code of `text`, centred, as large as fits in `size` pixels (dark modules in `fg`, white quiet zone).
    // Returns false if the text doesn't fit any QR version at that size.
    bool qr(const std::string& text, int x, int y, int size);

    // Copy the region [sx, sx+w) x [sy, sy+h) out as a new canvas (to slice a banner into keys).
    Canvas crop(int sx, int sy, int w, int h) const;

private:
    int w_, h_;
    std::vector<uint8_t> px_;
};

}  // namespace deck_ui
