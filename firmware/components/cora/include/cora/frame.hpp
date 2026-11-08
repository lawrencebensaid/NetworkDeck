// Cora framing: the 16-byte header the Stream Deck Network Dock wraps every TCP message in.
// Pure C++17, no ESP-IDF dependencies, so it builds and is tested on the Mac.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cora {

inline constexpr uint8_t kMagic[4] = {0x43, 0x93, 0x8A, 0x41};
inline constexpr size_t kHeaderSize = 16;
// Larger lengths can only be garbage; the decoder treats them as a false magic match.
inline constexpr uint32_t kMaxPayload = 64 * 1024;

namespace flags {
inline constexpr uint16_t kNone = 0x0000;
inline constexpr uint16_t kVerbatim = 0x8000;  // payload is for/from the child deck
inline constexpr uint16_t kReqAck = 0x4000;    // host asks for an ACK
inline constexpr uint16_t kAckNak = 0x0200;    // response to REQ_ACK (and keepalive acks)
inline constexpr uint16_t kResult = 0x0100;    // response to GET_REPORT
}  // namespace flags

enum class HidOp : uint8_t {
    Write = 0x00,       // hid_write
    SendReport = 0x01,  // hid_send_feature_report
    GetReport = 0x02,   // hid_get_feature_report
};

struct Frame {
    uint16_t flags = flags::kNone;
    HidOp op = HidOp::Write;
    uint32_t message_id = 0;
    std::vector<uint8_t> payload;
};

std::vector<uint8_t> encode(const Frame& frame);

// One-line description for logs: flags, op, id, length and the first payload bytes.
std::string describe(const Frame& frame);

// Streaming decoder: feed raw TCP bytes, pull complete frames. Resynchronizes on the magic bytes,
// so garbage between frames is skipped.
class Decoder {
public:
    void feed(const uint8_t* data, size_t len);
    std::optional<Frame> next();
    size_t buffered() const { return buf_.size() - pos_; }
    size_t discarded() const { return discarded_; }

private:
    void drop(size_t n);
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    size_t discarded_ = 0;
};

}  // namespace cora
