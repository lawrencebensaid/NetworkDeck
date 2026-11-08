// Offsets here are the ones node-elgato-stream-deck packages/tcp reads (commit 7dd305c).
#include <doctest/doctest.h>

#include <string>

#include "cora/protocol.hpp"

using namespace cora;

static uint16_t u16(const std::vector<uint8_t>& b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }

static DockIdentity identity() {
    DockIdentity id;
    id.serial = "D3E8F60AEB204C";
    id.firmware = "1.00.000";
    id.mac = {0xE8, 0xF6, 0x0A, 0xEB, 0x20, 0x4C};
    return id;
}

static ChildInfo mk2() {
    ChildInfo c;
    c.attached = true;
    c.vid = 0x0FD9;
    c.pid = 0x0080;
    c.serial = "A00TA3422LRSUR";
    c.tcp_port = 5344;
    return c;
}

TEST_CASE("0x80 device info reports the Elgato VID and the dock PID 0xFFFF") {
    auto b = dock_report(report::kDeviceInfo, identity(), mk2());
    REQUIRE(b.size() == kReportSize);
    CHECK(b[0] == 0x03);
    CHECK(b[1] == 0x80);
    CHECK(u16(b, 12) == 0x0FD9);
    CHECK(u16(b, 14) == 0xFFFF);
}

TEST_CASE("0x83 firmware version is ASCII at 8..16") {
    auto b = dock_report(report::kFirmware, identity(), mk2());
    CHECK(b[1] == 0x83);
    CHECK(std::string(b.begin() + 8, b.begin() + 16) == "1.00.000");
}

TEST_CASE("0x84 serial has its length at byte 3 and ASCII from byte 4") {
    auto b = dock_report(report::kSerial, identity(), mk2());
    CHECK(b[1] == 0x84);
    REQUIRE(b[3] == 14);
    CHECK(std::string(b.begin() + 4, b.begin() + 4 + 14) == "D3E8F60AEB204C");
}

TEST_CASE("0x85 MAC is at 4..10") {
    auto b = dock_report(report::kMac, identity(), mk2());
    CHECK(b[1] == 0x85);
    CHECK(b[4] == 0xE8);
    CHECK(b[9] == 0x4C);
}

TEST_CASE("unknown dock report ids produce no reply") {
    CHECK(dock_report(0x42, identity(), mk2()).empty());
}

TEST_CASE("device 2 info describes an attached deck (reply form 03 1c, push form 01 0b)") {
    auto b = device2_info(mk2());
    CHECK(b[0] == 0x03);
    CHECK(b[1] == 0x1C);
    CHECK(u16(b, 2) == 124);
    CHECK(u16(b, 30) == 0x0100);
    CHECK(std::string(reinterpret_cast<const char*>(&b[64])) == "Elgato");
    auto push = device2_info(mk2(), true);
    CHECK(push[0] == 0x01);
    CHECK(push[1] == 0x0B);
    CHECK(b[4] == 0x02);
    CHECK(u16(b, 26) == 0x0FD9);
    CHECK(u16(b, 28) == 0x0080);
    CHECK(std::string(reinterpret_cast<const char*>(&b[94])) == "A00TA3422LRSUR");
    CHECK(u16(b, 126) == 5344);
}

TEST_CASE("device 2 info says nothing is connected when detached") {
    ChildInfo none;
    auto b = device2_info(none, true);
    CHECK(b[0] == 0x01);
    CHECK(b[1] == 0x0B);
    CHECK(b[4] != 0x02);
}

TEST_CASE("a 31-char deck serial fills 94..125 without overrunning the port field") {
    auto c = mk2();
    c.serial = std::string(40, 'X');
    auto b = device2_info(c);
    CHECK(b[124] == 'X');
    CHECK(u16(b, 126) == 5344);
}

TEST_CASE("keepalive payload and its ack") {
    auto k = keepalive_payload(7);
    CHECK(k.size() > 4);
    CHECK(k[0] == 0x01);
    CHECK(k[1] == 0x0A);
    CHECK(k[5] == 7);

    Frame ack;
    ack.flags = flags::kAckNak;
    ack.payload = {0x03, 0x1A, 7};
    CHECK(is_keepalive_ack(ack));
    ack.flags = flags::kNone;
    CHECK_FALSE(is_keepalive_ack(ack));
}

TEST_CASE("client count report (03 8f): length 2, bytes 01 00 (what the Stream Deck app pairs with)") {
    DockIdentity id;
    id.serial = "D3E8F60AEB204C";
    id.firmware = "1.00.000";
    auto r = dock_report(report::kClientCount, id, ChildInfo{});
    REQUIRE(r.size() == kReportSize);
    CHECK(r[0] == 0x03);
    CHECK(r[1] == 0x8F);
    CHECK(r[2] == 0x02);
    CHECK(r[3] == 0x00);
    CHECK(r[4] == 0x01);
    CHECK(r[5] == 0x00);
}

TEST_CASE("0x87 is the network configuration; other 0x8x reports get an empty reply") {
    DockIdentity id;
    id.serial = "D3E8F60AEB204C";
    id.firmware = "1.00.000";
    id.ip = {10, 0, 1, 187};
    id.netmask = {255, 255, 255, 0};
    auto r = dock_report(0x87, id, ChildInfo{});
    REQUIRE(r.size() == kReportSize);
    CHECK(r[1] == 0x87);
    CHECK(r[2] == 1);
    CHECK(r[3] == 1);
    CHECK(r[4] == 10);
    CHECK(r[7] == 187);
    CHECK(r[8] == 255);
    CHECK((r[16] | (r[17] << 8)) == 5343);
    auto e = dock_report(0x8B, id, ChildInfo{});
    REQUIRE(e.size() == kReportSize);
    CHECK(e[0] == 0x03);
    CHECK(e[1] == 0x8B);
    CHECK(dock_report(0x42, id, ChildInfo{}).empty());
}

TEST_CASE("device 2 info is 128 bytes, like a real dock") {
    CHECK(device2_info(mk2()).size() == kDevice2InfoSize);
    CHECK(device2_info(ChildInfo{}).size() == kDevice2InfoSize);
}

TEST_CASE("keepalive ack: only the ACK_NAK form (the app's plain 03 1a Write is its watchdog poll)") {
    Frame a;
    a.flags = flags::kAckNak;
    a.payload = {0x03, 0x1A, 0x01};
    CHECK(is_keepalive_ack(a));
    Frame b;
    b.flags = flags::kNone;
    b.op = HidOp::Write;
    b.payload.assign(1024, 0);
    b.payload[0] = 0x03;
    b.payload[1] = 0x1A;
    CHECK_FALSE(is_keepalive_ack(b));
}

TEST_CASE("0x87 preserves non-/24 DHCP masks and both copies of the active IPv4 address") {
    const std::array<std::array<uint8_t, 4>, 3> masks{{
        {255, 255, 0, 0}, {255, 255, 254, 0}, {255, 255, 255, 128}
    }};
    for (const auto& mask : masks) {
        auto id = identity();
        id.ip = {10, 42, 7, 99};
        id.netmask = mask;
        const auto reply = dock_report(report::kNetworkConfig, id, mk2());
        REQUIRE(reply.size() == kReportSize);
        for (size_t i = 0; i < 4; ++i) {
            CHECK(reply[4 + i] == id.ip[i]);
            CHECK(reply[8 + i] == mask[i]);
            CHECK(reply[12 + i] == id.ip[i]);
        }
        CHECK(u16(reply, 16) == kDockPort);
    }
}

TEST_CASE("Ethernet to Wi-Fi identity update replaces both addresses and mask, not serial or MAC") {
    auto id = identity();
    id.ip = {192, 168, 1, 50};
    id.netmask = {255, 255, 255, 0};
    const auto serial = dock_report(report::kSerial, id, mk2());
    const auto mac = dock_report(report::kMac, id, mk2());
    id.ip = {10, 42, 7, 99};
    id.netmask = {255, 255, 0, 0};
    const auto wifi = dock_report(report::kNetworkConfig, id, mk2());
    CHECK(wifi[4] == 10);
    CHECK(wifi[7] == 99);
    CHECK(wifi[10] == 0);
    CHECK(wifi[12] == 10);
    CHECK(wifi[15] == 99);
    CHECK(dock_report(report::kSerial, id, mk2()) == serial);
    CHECK(dock_report(report::kMac, id, mk2()) == mac);
}

TEST_CASE("DHCP mask-only renewal is represented without changing the IP") {
    auto id = identity();
    id.ip = {10, 42, 7, 99};
    id.netmask = {255, 255, 255, 0};
    const auto before = dock_report(report::kNetworkConfig, id, mk2());
    id.netmask = {255, 255, 254, 0};
    const auto after = dock_report(report::kNetworkConfig, id, mk2());
    CHECK(before[10] == 255);
    CHECK(after[10] == 254);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(before[4 + i] == after[4 + i]);
        CHECK(before[12 + i] == after[12 + i]);
    }
}
