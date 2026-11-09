// Per-model facts for the decks the dock draws on itself. Values from node-elgato-stream-deck
// packages/core/src/models (definitions.ts, generic-gen1/gen2.ts, 6-key.ts, imageWriter).
#pragma once

#include <cstdint>
#include <vector>

#include "deck_ui/canvas.hpp"

namespace deck_ui {

struct Model {
    enum class Format { Jpeg, Bmp };
    enum class Header { Gen1, Gen2 };

    const char* name;
    int cols, rows;
    int key_px;
    Format format;
    Header header;
    bool x_flip, y_flip, rotate, bgr;
    int key_data_offset;  // where key states start in an input report, after the report id
    int bmp_ppm;

    int keys() const { return cols * rows; }
};

// nullptr for decks we don't draw on (we still bridge them for the host).
const Model* find_model(uint16_t pid);

// The key image in the deck's orientation and colour order: rows of 3-byte pixels, no header.
std::vector<uint8_t> transform(const Model& m, const Canvas& key);
// A 54-byte BITMAPINFOHEADER BMP around `pixels` (gen1 decks).
std::vector<uint8_t> bmp(const Model& m, const std::vector<uint8_t>& pixels);
// Split an encoded image into the deck's 1024-byte image write reports.
std::vector<std::vector<uint8_t>> image_reports(const Model& m, int key, const std::vector<uint8_t>& image);
// Feature report that sets brightness (0..100).
std::vector<uint8_t> brightness_report(const Model& m, int percent);
// Key states from an input report (empty if it isn't a key report).
std::vector<bool> key_states(const Model& m, const uint8_t* report, size_t len);

// One image write report from the host, split up (for the web interface's live view).
struct ImagePart {
    int key = 0;
    int part = 0;
    bool last = false;
    const uint8_t* data = nullptr;
    size_t len = 0;
};
bool parse_image_report(const Model& m, const uint8_t* report, size_t len, ImagePart& out);

// A gen2 solid-colour fill (feature 03 06 <key> <r> <g> <b>).
bool parse_fill_report(const Model& m, const uint8_t* report, size_t len, int& key, Rgb& color);

// CSS transform that turns a key image in the deck's orientation upright again.
const char* css_transform(const Model& m);

}  // namespace deck_ui
