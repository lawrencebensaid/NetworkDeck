#include "cora/session.hpp"

#include <utility>

namespace cora {

Session::Session(Port port, uint8_t conn_no, uint32_t now_ms)
    : port_(port), conn_no_(conn_no), last_rx_ms_(now_ms), last_keepalive_ms_(now_ms) {}

Frame Session::keepalive() {
    Frame f;
    f.flags = flags::kNone;
    f.op = HidOp::Write;
    f.message_id = 0;
    f.payload = keepalive_payload(conn_no_);
    return f;
}

Frame Session::greeting(uint32_t now_ms) {
    last_keepalive_ms_ = now_ms;
    return keepalive();
}

std::vector<Action> Session::on_bytes(const uint8_t* data, size_t len, const DockIdentity& identity,
                                      const ChildInfo& child, uint32_t now_ms) {
    std::vector<Action> out;
    if (len > 0) last_rx_ms_ = now_ms;
    decoder_.feed(data, len);
    while (auto f = decoder_.next()) handle(*f, identity, child, out);
    return out;
}

std::vector<Action> Session::on_tick(uint32_t now_ms) {
    std::vector<Action> out;
    if (now_ms - last_keepalive_ms_ >= kKeepaliveIntervalMs) {
        last_keepalive_ms_ = now_ms;
        out.push_back({Action::Kind::Send, keepalive()});
    }
    return out;
}

bool Session::timed_out(uint32_t now_ms) const { return now_ms - last_rx_ms_ >= kHostTimeoutMs; }

void Session::handle(const Frame& f, const DockIdentity& identity, const ChildInfo& child,
                     std::vector<Action>& out) {
    if (is_keepalive_ack(f)) return;
    route(f, identity, child, out);
    if (f.flags & flags::kReqAck) {
        Frame ack;
        ack.flags = flags::kAckNak;
        ack.op = f.op;
        ack.message_id = f.message_id;
        out.push_back({Action::Kind::Send, std::move(ack)});
    }
}

void Session::route(const Frame& f, const DockIdentity& identity, const ChildInfo& child,
                    std::vector<Action>& out) {

    const bool verbatim = f.flags & flags::kVerbatim;

    if (!verbatim) {
        // Dock-level requests: `03 <id>`. Both connections answer device 2 info.
        if (f.op == HidOp::GetReport && f.payload.size() >= 2 && f.payload[0] == 0x03) {
            const uint8_t id = f.payload[1];
            if (port_ == Port::Child && id != report::kDevice2Info) {
                // The host probes 0x80 on every socket to tell dock from deck. Silence = deck.
                if (id != report::kDeviceInfo) out.push_back({Action::Kind::Unknown, f});
                return;
            }
            auto payload = dock_report(id, identity, child);
            if (payload.empty()) {
                out.push_back({Action::Kind::Unknown, f});
                return;
            }
            Frame r;
            r.flags = flags::kResult;
            r.op = HidOp::GetReport;
            r.message_id = f.message_id;
            r.payload = std::move(payload);
            out.push_back({Action::Kind::Send, std::move(r)});
            return;
        }
        if (port_ == Port::Dock && f.op == HidOp::SendReport && f.payload.size() >= 2 && f.payload[0] == 0x03 &&
            f.payload[1] == 0x08) {
            return;  // brightness on the dock itself: accepted, nothing to do
        }
        out.push_back({Action::Kind::Unknown, f});
    } else if (port_ == Port::Child && !f.payload.empty()) {
        switch (f.op) {
            case HidOp::Write:
                out.push_back({Action::Kind::UsbWrite, f});
                break;
            case HidOp::SendReport:
                out.push_back({Action::Kind::UsbSetFeature, f});
                break;
            case HidOp::GetReport:
                out.push_back({Action::Kind::UsbGetFeature, f});
                break;
            default:
                out.push_back({Action::Kind::Unknown, f});
        }
    } else {
        out.push_back({Action::Kind::Unknown, f});
    }
}

Frame Session::input_frame(const uint8_t* report, size_t len) {
    Frame f;
    f.flags = flags::kVerbatim;
    f.op = HidOp::Write;
    f.payload.assign(report, report + len);
    return f;
}

Frame Session::feature_result(uint32_t message_id, std::vector<uint8_t> report) {
    Frame f;
    f.flags = flags::kVerbatim | flags::kResult;
    f.op = HidOp::GetReport;
    f.message_id = message_id;
    f.payload = std::move(report);
    return f;
}

Frame Session::plug_event(const ChildInfo& child) {
    Frame f;
    f.flags = flags::kNone;
    f.op = HidOp::Write;
    f.payload = device2_info(child);
    return f;
}

}  // namespace cora
