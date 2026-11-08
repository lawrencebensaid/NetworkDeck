#include "cora/protocol.hpp"

#include <algorithm>
#include <cstring>

namespace cora {
namespace {

void put_u16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}

void put_ascii(std::vector<uint8_t>& b, size_t at, const std::string& s, size_t max) {
    std::memcpy(&b[at], s.data(), std::min(s.size(), max));
}

std::vector<uint8_t> report_header(uint8_t id) {
    std::vector<uint8_t> b(kReportSize, 0);
    b[0] = 0x03;
    b[1] = id;
    return b;
}

}  // namespace

std::vector<uint8_t> device2_info(const ChildInfo& child) {
    std::vector<uint8_t> b(kReportSize, 0);
    b[0] = 0x01;
    b[1] = 0x0B;
    if (!child.attached) return b;  // byte 4 != 0x02 means "nothing connected"
    b[4] = 0x02;
    put_u16(b, 26, child.vid);
    put_u16(b, 28, child.pid);
    put_ascii(b, 94, child.serial, 31);  // 94..125, NUL-terminated when shorter
    put_u16(b, 126, child.tcp_port);
    return b;
}

std::vector<uint8_t> dock_report(uint8_t id, const DockIdentity& identity, const ChildInfo& child) {
    switch (id) {
        case report::kDeviceInfo: {
            auto b = report_header(id);
            put_u16(b, 12, kElgatoVid);
            put_u16(b, 14, kDockPid);
            return b;
        }
        case report::kFirmware: {
            auto b = report_header(id);
            put_ascii(b, 8, identity.firmware, 8);
            return b;
        }
        case report::kSerial: {
            auto b = report_header(id);
            size_t n = std::min<size_t>(identity.serial.size(), 32);
            b[3] = static_cast<uint8_t>(n);
            put_ascii(b, 4, identity.serial, n);
            return b;
        }
        case report::kMac: {
            auto b = report_header(id);
            std::copy(identity.mac.begin(), identity.mac.end(), b.begin() + 4);
            return b;
        }
        case report::kDevice2Info:
            return device2_info(child);
        default:
            return {};
    }
}

std::vector<uint8_t> keepalive_payload(uint8_t conn_no) {
    std::vector<uint8_t> b(32, 0);
    b[0] = 0x01;
    b[1] = 0x0A;
    b[5] = conn_no;
    return b;
}

bool is_keepalive_ack(const Frame& frame) {
    return (frame.flags & flags::kAckNak) && frame.payload.size() >= 2 && frame.payload[0] == 0x03 &&
           frame.payload[1] == 0x1A;
}

}  // namespace cora
