#include "cora/frame.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cora {
namespace {

void put_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void put_u32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint16_t get_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t get_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

std::vector<uint8_t> encode(const Frame& frame) {
    std::vector<uint8_t> out(kHeaderSize + frame.payload.size(), 0);
    std::memcpy(out.data(), kMagic, sizeof(kMagic));
    put_u16(&out[4], frame.flags);
    out[6] = static_cast<uint8_t>(frame.op);
    put_u32(&out[8], frame.message_id);
    put_u32(&out[12], static_cast<uint32_t>(frame.payload.size()));
    if (!frame.payload.empty()) std::memcpy(&out[kHeaderSize], frame.payload.data(), frame.payload.size());
    return out;
}

std::string describe(const Frame& frame) {
    char head[96];
    std::snprintf(head, sizeof(head), "flags=%04x op=%u id=%08x len=%u", frame.flags,
                  static_cast<unsigned>(frame.op), static_cast<unsigned>(frame.message_id),
                  static_cast<unsigned>(frame.payload.size()));
    std::string s = head;
    if (!frame.payload.empty()) s += " :";
    size_t n = std::min<size_t>(frame.payload.size(), 32);
    for (size_t i = 0; i < n; ++i) {
        char b[4];
        std::snprintf(b, sizeof(b), " %02x", frame.payload[i]);
        s += b;
    }
    if (frame.payload.size() > n) s += " …";
    return s;
}

void Decoder::feed(const uint8_t* data, size_t len) {
    // Compact before growing so the buffer doesn't creep with every frame.
    if (pos_ > 0 && pos_ >= buf_.size() / 2) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos_));
        pos_ = 0;
    }
    buf_.insert(buf_.end(), data, data + len);
}

void Decoder::drop(size_t n) {
    pos_ += n;
    if (pos_ >= buf_.size()) {
        buf_.clear();
        pos_ = 0;
    }
}

std::optional<Frame> Decoder::next() {
    for (;;) {
        const uint8_t* begin = buf_.data() + pos_;
        size_t avail = buf_.size() - pos_;
        if (avail < sizeof(kMagic)) return std::nullopt;

        // Find the magic. Keep the last 3 bytes if it isn't there: they may start the next one.
        const uint8_t* hit = std::search(begin, begin + avail, kMagic, kMagic + sizeof(kMagic));
        if (hit == begin + avail) {
            size_t skip = avail - (sizeof(kMagic) - 1);
            discarded_ += skip;
            drop(skip);
            return std::nullopt;
        }
        if (hit != begin) {
            size_t skip = static_cast<size_t>(hit - begin);
            discarded_ += skip;
            drop(skip);
            continue;
        }

        if (avail < kHeaderSize) return std::nullopt;
        uint32_t len = get_u32(begin + 12);
        if (len > kMaxPayload) {
            // Not a real header: skip past this magic and look again.
            discarded_ += 1;
            drop(1);
            continue;
        }
        if (avail < kHeaderSize + len) return std::nullopt;

        Frame f;
        f.flags = get_u16(begin + 4);
        f.op = static_cast<HidOp>(begin[6]);
        f.message_id = get_u32(begin + 8);
        f.payload.assign(begin + kHeaderSize, begin + kHeaderSize + len);
        drop(kHeaderSize + len);
        return f;
    }
}

}  // namespace cora
