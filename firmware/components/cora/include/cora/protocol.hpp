// The dock's own reports, as read by node-elgato-stream-deck packages/tcp (commit 7dd305c).
// Byte offsets are into the Cora payload. Fields the library doesn't read are zero.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cora/frame.hpp"

namespace cora {

inline constexpr uint16_t kElgatoVid = 0x0FD9;
inline constexpr uint16_t kDockPid = 0xFFFF;  // what the Network Dock reports over TCP
inline constexpr uint16_t kDockPort = 5343;
inline constexpr uint16_t kChildPort = 5344;  // our choice; announced in device 2 info
inline constexpr size_t kReportSize = 1024;   // assumption, to check against the official app

namespace report {
inline constexpr uint8_t kDeviceInfo = 0x80;
inline constexpr uint8_t kFirmware = 0x83;
inline constexpr uint8_t kSerial = 0x84;
inline constexpr uint8_t kMac = 0x85;
inline constexpr uint8_t kDevice2Info = 0x1C;
// Number of client connections: 03 8f <len u16 = 2> <a> <b>. The Stream Deck app asks before pairing.
inline constexpr uint8_t kClientCount = 0x8F;
}  // namespace report

struct DockIdentity {
    std::string serial;    // 14 uppercase alphanumerics
    std::string firmware;  // up to 8 ASCII chars, e.g. "1.00.000"
    std::array<uint8_t, 6> mac{};
};

struct ChildInfo {
    bool attached = false;
    uint16_t vid = 0;
    uint16_t pid = 0;
    std::string serial;
    uint16_t tcp_port = kChildPort;
};

// Reply payload for a GET_REPORT `03 <id>` on the dock connection. Empty if the id is unknown.
std::vector<uint8_t> dock_report(uint8_t id, const DockIdentity& identity, const ChildInfo& child);

// Device 2 info (`01 0B …`): reply to report 0x1C and, unprompted, the plug event.
std::vector<uint8_t> device2_info(const ChildInfo& child);

// Keepalive the dock sends: `01 0A`, connection number at byte 5.
std::vector<uint8_t> keepalive_payload(uint8_t conn_no);

// The host's answer to a keepalive: ACK_NAK with payload `03 1A <conn_no>`.
bool is_keepalive_ack(const Frame& frame);

}  // namespace cora
