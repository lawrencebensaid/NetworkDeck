// The dock's own reports. Sources: node-elgato-stream-deck packages/tcp (commit 7dd305c), OpenNetDeck
// (github.com/Yeradon/OpenNetDeck, verified with the official app) and tracing the Stream Deck app.
// Byte offsets are into the Cora payload.
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
inline constexpr size_t kDevice2InfoSize = 128;
inline constexpr size_t kReportSize = 1024;   // replies to 1024-byte requests (the Stream Deck app's form)
inline constexpr size_t kShortReplySize = 128;  // replies to short requests (the open-source client's form)

namespace report {
inline constexpr uint8_t kDeviceInfo = 0x80;
inline constexpr uint8_t kFirmware = 0x83;
inline constexpr uint8_t kSerial = 0x84;
inline constexpr uint8_t kMac = 0x85;
inline constexpr uint8_t kDevice2Info = 0x1C;
// Number of client connections: 03 8f <len u16 = 2> <a> <b>. The Stream Deck app asks before pairing.
inline constexpr uint8_t kClientCount = 0x8F;
// Network configuration: [2] 1, [3] 1, IP at 4, netmask at 8, IP again at 12, port at 16.
inline constexpr uint8_t kNetworkConfig = 0x87;
// The Stream Deck app's watchdog poll, once a second. After answering the first one, the dock pushes
// the hot-plug event (01 0B) for an attached deck: that is what makes the app open the deck port.
inline constexpr uint8_t kWatchdog = 0x1A;
}  // namespace report

struct DockIdentity {
    std::string serial;    // 14 uppercase alphanumerics
    std::string firmware;  // up to 8 ASCII chars, e.g. "1.00.000"
    std::array<uint8_t, 6> mac{};
    // Bytes 4-5 of the 03 8f report. The Stream Deck app pairs with 01 00; 00 01 and 01 01 make it
    // report "Device is already in use".
    std::array<uint8_t, 2> client_count{0x01, 0x00};
    std::array<uint8_t, 4> ip{}, netmask{};  // for 0x87
    uint16_t port = kDockPort;
};

struct ChildInfo {
    bool attached = false;
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint16_t bcd_device = 0x0100;
    std::string name;    // USB product string, e.g. "Stream Deck MK.2"
    std::string serial;
    uint16_t tcp_port = kChildPort;
    uint8_t slot = 0;
};

// Reply payload (kReportSize bytes) for `03 <id>` on the dock connection; `arg` is request byte 2 (the
// slot for 0x1C). Empty if the id is unknown.
std::vector<uint8_t> dock_report(uint8_t id, const DockIdentity& identity, const ChildInfo& child, uint8_t arg = 0);

// Device 2 info, 128 bytes: `03 1C …` as the reply to report 0x1C for `slot`, `01 0B …` as the
// unprompted hot-plug event.
std::vector<uint8_t> device2_info(const ChildInfo& child, bool push = false, uint8_t slot = 0);

// Keepalive the dock sends: `01 0A`, connection number at byte 5.
std::vector<uint8_t> keepalive_payload(uint8_t conn_no);

// The host's answer to a keepalive: ACK_NAK with payload `03 1A <conn_no>`.
bool is_keepalive_ack(const Frame& frame);

}  // namespace cora
