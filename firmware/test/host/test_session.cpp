#include <doctest/doctest.h>

#include "cora/session.hpp"

using namespace cora;

namespace {

DockIdentity identity() {
    DockIdentity id;
    id.serial = "D3E8F60AEB204C";
    id.firmware = "1.00.000";
    return id;
}

ChildInfo mk2() {
    ChildInfo c;
    c.attached = true;
    c.vid = 0x0FD9;
    c.pid = 0x0080;
    c.serial = "A00TA3422LRSUR";
    return c;
}

std::vector<Action> feed(Session& s, const Frame& f, uint32_t now = 100) {
    auto bytes = encode(f);
    return s.on_bytes(bytes.data(), bytes.size(), identity(), mk2(), now);
}

Frame host_get(uint8_t id, uint32_t msg = 0x1234) {
    Frame f;
    f.flags = flags::kNone;
    f.op = HidOp::GetReport;
    f.message_id = msg;
    f.payload = {0x03, id};
    return f;
}

Frame verbatim(HidOp op, std::vector<uint8_t> payload, uint32_t msg = 0) {
    Frame f;
    f.flags = flags::kVerbatim;
    f.op = op;
    f.message_id = msg;
    f.payload = std::move(payload);
    return f;
}

}  // namespace

TEST_CASE("greeting is a keepalive, so the host detects Cora framing") {
    Session s(Port::Dock, 3, 0);
    auto g = s.greeting(0);
    auto bytes = encode(g);
    CHECK(bytes[0] == 0x43);
    CHECK(g.payload[0] == 0x01);
    CHECK(g.payload[1] == 0x0A);
    CHECK(g.payload[5] == 3);
}

TEST_CASE("dock port answers 0x80 with a RESULT echoing the message id") {
    Session s(Port::Dock, 1, 0);
    auto acts = feed(s, host_get(report::kDeviceInfo, 0xBEEF));
    REQUIRE(acts.size() == 1);
    CHECK(acts[0].kind == Action::Kind::Send);
    CHECK(acts[0].frame.flags == flags::kResult);
    CHECK(acts[0].frame.message_id == 0xBEEF);
    CHECK(acts[0].frame.payload[1] == 0x80);
}

TEST_CASE("dock port answers device 2 info") {
    Session s(Port::Dock, 1, 0);
    auto acts = feed(s, host_get(report::kDevice2Info));
    REQUIRE(acts.size() == 1);
    CHECK(acts[0].frame.payload[0] == 0x01);
    CHECK(acts[0].frame.payload[1] == 0x0B);
    CHECK(acts[0].frame.payload[4] == 0x02);
}

TEST_CASE("child port stays silent on the 0x80 probe but answers device 2 info") {
    Session s(Port::Child, 1, 0);
    CHECK(feed(s, host_get(report::kDeviceInfo)).empty());
    auto acts = feed(s, host_get(report::kDevice2Info));
    REQUIRE(acts.size() == 1);
    CHECK(acts[0].kind == Action::Kind::Send);
    CHECK(acts[0].frame.payload[1] == 0x0B);
}

TEST_CASE("child port maps verbatim ops onto USB actions") {
    Session s(Port::Child, 1, 0);
    auto w = feed(s, verbatim(HidOp::Write, {0x02, 0x07, 0x00}));
    REQUIRE(w.size() == 1);
    CHECK(w[0].kind == Action::Kind::UsbWrite);
    CHECK(w[0].frame.payload[0] == 0x02);

    auto sf = feed(s, verbatim(HidOp::SendReport, {0x03, 0x08, 50}));
    REQUIRE(sf.size() == 1);
    CHECK(sf[0].kind == Action::Kind::UsbSetFeature);

    auto gf = feed(s, verbatim(HidOp::GetReport, {0x08}, 77));
    REQUIRE(gf.size() == 1);
    CHECK(gf[0].kind == Action::Kind::UsbGetFeature);
    CHECK(gf[0].frame.payload[0] == 0x08);
    CHECK(gf[0].frame.message_id == 77);
}

TEST_CASE("verbatim traffic on the dock port is reported as unknown") {
    Session s(Port::Dock, 1, 0);
    auto acts = feed(s, verbatim(HidOp::Write, {0x02}));
    REQUIRE(acts.size() == 1);
    CHECK(acts[0].kind == Action::Kind::Unknown);
}

TEST_CASE("dock brightness is accepted silently") {
    Session s(Port::Dock, 1, 0);
    Frame f;
    f.op = HidOp::SendReport;
    f.payload = {0x03, 0x08, 40};
    CHECK(feed(s, f).empty());
}

TEST_CASE("REQ_ACK gets an ACK_NAK with the same message id") {
    Session s(Port::Dock, 1, 0);
    Frame f;
    f.flags = flags::kReqAck;
    f.op = HidOp::SendReport;
    f.message_id = 55;
    f.payload = {0x03, 0x08, 40};
    auto acts = feed(s, f);
    REQUIRE(acts.size() == 1);
    CHECK(acts[0].kind == Action::Kind::Send);
    CHECK(acts[0].frame.flags == flags::kAckNak);
    CHECK(acts[0].frame.message_id == 55);
}

TEST_CASE("keepalive acks are absorbed") {
    Session s(Port::Dock, 4, 0);
    Frame ack;
    ack.flags = flags::kAckNak;
    ack.payload = {0x03, 0x1A, 4};
    CHECK(feed(s, ack).empty());
}

TEST_CASE("keepalive every second, timeout after five seconds of silence") {
    Session s(Port::Dock, 1, 0);
    s.greeting(0);
    CHECK(s.on_tick(500).empty());
    auto t = s.on_tick(1000);
    REQUIRE(t.size() == 1);
    CHECK(t[0].frame.payload[1] == 0x0A);
    CHECK_FALSE(s.timed_out(4999));
    CHECK(s.timed_out(5000));

    Frame ack;
    ack.flags = flags::kAckNak;
    ack.payload = {0x03, 0x1A, 1};
    feed(s, ack, 4800);
    CHECK_FALSE(s.timed_out(9000));
    CHECK(s.timed_out(9800));
}

TEST_CASE("input and feature result frames are verbatim") {
    uint8_t report[] = {0x01, 0x00, 0x0F, 0x00, 0x01};
    auto in = Session::input_frame(report, sizeof(report));
    CHECK((in.flags & flags::kVerbatim) != 0);
    CHECK(in.payload[0] == 0x01);

    auto r = Session::feature_result(9, {0x08, 0x31});
    CHECK(r.flags == (flags::kVerbatim | flags::kResult));
    CHECK(r.message_id == 9);
    CHECK(r.op == HidOp::GetReport);
}
