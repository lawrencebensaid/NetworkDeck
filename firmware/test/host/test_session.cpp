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
    // Reply form 03 1C (the open-source client reads it by offset either way); 128 bytes for a short request.
    CHECK(acts[0].frame.payload[0] == 0x03);
    CHECK(acts[0].frame.payload[1] == 0x1C);
    CHECK(acts[0].frame.payload[4] == 0x02);
    CHECK(acts[0].frame.payload.size() == 128);
    CHECK(acts[0].frame.op == HidOp::GetReport);
}

TEST_CASE("child port stays silent on the 0x80 probe but answers device 2 info") {
    Session s(Port::Child, 1, 0);
    CHECK(feed(s, host_get(report::kDeviceInfo)).empty());
    auto acts = feed(s, host_get(report::kDevice2Info));
    REQUIRE(acts.size() == 1);
    CHECK(acts[0].kind == Action::Kind::Send);
    CHECK(acts[0].frame.payload[1] == 0x1C);
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

TEST_CASE("dock port also bridges verbatim deck traffic (the official app uses only this connection)") {
    Session s(Port::Dock, 1, 0);
    auto w = feed(s, verbatim(HidOp::Write, {0x02, 0x07, 0x00}));
    REQUIRE(w.size() == 1);
    CHECK(w[0].kind == Action::Kind::UsbWrite);
    auto gf = feed(s, verbatim(HidOp::GetReport, {0x08}, 5));
    REQUIRE(gf.size() == 1);
    CHECK(gf[0].kind == Action::Kind::UsbGetFeature);
}

TEST_CASE("dock brightness is accepted silently") {
    Session s(Port::Dock, 1, 0);
    Frame f;
    f.op = HidOp::SendReport;
    f.payload = {0x03, 0x08, 40};
    CHECK(feed(s, f).empty());
}

TEST_CASE("a write that asks for an ack gets a 1-byte RESULT with the same op and message id") {
    Session s(Port::Child, 1, 0);
    auto acts = feed(s, verbatim(HidOp::SendReport, {0x03, 0x08, 40}, 55));
    REQUIRE(acts.size() == 2);
    CHECK(acts[0].kind == Action::Kind::UsbSetFeature);
    CHECK(acts[1].kind == Action::Kind::Send);
    CHECK(acts[1].frame.flags == (flags::kResult | flags::kVerbatim));
    CHECK(acts[1].frame.op == HidOp::SendReport);
    CHECK(acts[1].frame.message_id == 55);
    REQUIRE(acts[1].frame.payload.size() == 1);
    CHECK(acts[1].frame.payload[0] == 0x03);
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

TEST_CASE("input frames are plain Writes; feature results follow the request's form") {
    uint8_t report[] = {0x01, 0x00, 0x0F, 0x00, 0x01};
    auto in = Session::input_frame(report, sizeof(report));
    CHECK(in.flags == flags::kNone);
    CHECK(in.op == HidOp::Write);
    CHECK(in.payload[0] == 0x01);

    Action v{Action::Kind::UsbGetFeature, verbatim(HidOp::GetReport, {0x08}, 9), 0x08};
    auto r = Session::feature_result(v, {0x08, 0x31});
    CHECK(r.flags == (flags::kVerbatim | flags::kResult));
    CHECK(r.message_id == 9);
    CHECK(r.op == HidOp::GetReport);

    Frame app;
    app.op = HidOp::Write;
    app.payload.assign(1024, 0);
    app.payload[0] = 0x03;
    app.payload[1] = 0x05;
    Action a{Action::Kind::UsbGetFeature, app, 0x05};
    auto r2 = Session::feature_result(a, {0x05, 0x0c});
    CHECK(r2.flags == flags::kResult);
    CHECK(r2.op == HidOp::Write);
    CHECK(r2.payload.size() == 1024);
}

TEST_CASE("dock port answers the Stream Deck app's Write-form request (03 <id>, 1024 bytes)") {
    // The official app asks for dock reports as a plain Write, padded to 1024 bytes, message id 0.
    Session s(Port::Dock, 1, 0);
    Frame req;
    req.flags = flags::kNone;
    req.op = HidOp::Write;
    req.message_id = 0;
    req.payload.assign(1024, 0);
    req.payload[0] = 0x03;
    req.payload[1] = 0x83;
    auto out = feed(s, req);
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == Action::Kind::Send);
    CHECK(out[0].frame.payload[0] == 0x03);
    CHECK(out[0].frame.payload[1] == 0x83);
    CHECK(std::string(out[0].frame.payload.begin() + 8, out[0].frame.payload.begin() + 16) == "1.00.000");
}

namespace {
Frame app_query(uint8_t id, uint8_t arg = 0) {
    Frame f;
    f.flags = flags::kNone;
    f.op = HidOp::Write;
    f.payload.assign(1024, 0);
    f.payload[0] = 0x03;
    f.payload[1] = id;
    f.payload[2] = arg;
    return f;
}
}  // namespace

TEST_CASE("the app's watchdog poll (03 1a) is answered, and the first one pushes the deck's hot-plug event") {
    Session s(Port::Dock, 1, 0);
    auto first = feed(s, app_query(0x1A));
    REQUIRE(first.size() == 2);
    CHECK(first[0].frame.flags == flags::kResult);
    CHECK(first[0].frame.op == HidOp::Write);
    CHECK(first[0].frame.payload.size() == 1024);
    CHECK(first[0].frame.payload[1] == 0x1A);
    CHECK(first[1].frame.payload[0] == 0x01);
    CHECK(first[1].frame.payload[1] == 0x0B);
    CHECK(first[1].frame.payload[4] == 0x02);
    auto second = feed(s, app_query(0x1A));
    CHECK(second.size() == 1);
}

TEST_CASE("device 2 info reply to the app: 03 1c, slot echoed, 1024 bytes for a 1024-byte request") {
    Session s(Port::Dock, 1, 0);
    auto acts = feed(s, app_query(0x1C, 0));
    REQUIRE(acts.size() == 1);
    const auto& p = acts[0].frame.payload;
    CHECK(p.size() == 1024);
    CHECK(p[0] == 0x03);
    CHECK(p[1] == 0x1C);
    CHECK(p[4] == 0x02);
}

TEST_CASE("child port: app-form feature queries go to the deck; 0x80 answered only in the app's form") {
    Session s(Port::Child, 1, 0);
    auto q = feed(s, app_query(0x05));
    REQUIRE(q.size() == 1);
    CHECK(q[0].kind == Action::Kind::UsbGetFeature);
    CHECK(q[0].report_id == 0x05);
    auto info = feed(s, app_query(0x80));
    REQUIRE(info.size() == 1);
    CHECK(info[0].frame.payload[12] == 0xD9);
    CHECK(info[0].frame.payload[14] == 0x80);
    CHECK(feed(s, host_get(0x80)).empty());  // the open-source client's probe: silence = deck
}

TEST_CASE("child port: the app's plain Writes and SendReports drive the deck") {
    Session s(Port::Child, 1, 0);
    Frame w;
    w.op = HidOp::Write;
    w.payload = {0x02, 0x07, 0x00, 0x01};
    auto a = feed(s, w);
    REQUIRE_FALSE(a.empty());
    CHECK(a[0].kind == Action::Kind::UsbWrite);
    Frame sr;
    sr.op = HidOp::SendReport;
    sr.payload = {0x03, 0x08, 50};
    auto b = feed(s, sr);
    REQUIRE_FALSE(b.empty());
    CHECK(b[0].kind == Action::Kind::UsbSetFeature);
}
