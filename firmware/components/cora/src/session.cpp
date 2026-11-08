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

namespace {

// Replies mirror the request's op; 1024-byte requests (the Stream Deck app) get 1024-byte replies,
// short ones (the open-source client) 128.
Frame reply_to(const Frame& req, std::vector<uint8_t> payload, uint16_t extra_flags = 0) {
    const size_t len = req.payload.size() >= kReportSize ? kReportSize : kShortReplySize;
    if (payload.size() < len) payload.resize(len, 0);
    if (payload.size() > len && len == kShortReplySize) payload.resize(len);
    Frame r;
    r.flags = static_cast<uint16_t>(flags::kResult | extra_flags);
    r.op = req.op;
    r.message_id = req.message_id;
    r.payload = std::move(payload);
    return r;
}

bool is_query(const Frame& f) {
    return (f.op == HidOp::GetReport || f.op == HidOp::Write) && f.payload.size() >= 2 && f.payload[0] == 0x03;
}

}  // namespace

void Session::handle(const Frame& f, const DockIdentity& identity, const ChildInfo& child,
                     std::vector<Action>& out) {
    if (is_keepalive_ack(f)) return;
    const bool verbatim = (f.flags & flags::kVerbatim) != 0;
    route(f, identity, child, out);
    // Writes and feature sets that ask for it get a 1-byte RESULT (as OpenNetDeck does for the app).
    const bool data_op = f.op == HidOp::SendReport || (f.op == HidOp::Write && (verbatim || !is_query(f)));
    if (data_op && ((f.flags & flags::kReqAck) || f.message_id != 0) && !f.payload.empty()) {
        Frame ack;
        ack.flags = static_cast<uint16_t>(flags::kResult | (verbatim ? flags::kVerbatim : 0));
        ack.op = f.op;
        ack.message_id = f.message_id;
        ack.payload = {f.payload[0]};
        out.push_back({Action::Kind::Send, std::move(ack)});
    }
}

void Session::route(const Frame& f, const DockIdentity& identity, const ChildInfo& child,
                    std::vector<Action>& out) {
    const bool verbatim = (f.flags & flags::kVerbatim) != 0;

    if (!verbatim && is_query(f)) {
        const uint8_t id = f.payload[1];
        const uint8_t arg = f.payload.size() >= 3 ? f.payload[2] : 0;
        if (port_ == Port::Child) {
            if (id == report::kDevice2Info) {
                out.push_back({Action::Kind::Send, reply_to(f, device2_info(child, false, arg))});
            } else if (id == report::kWatchdog) {
                out.push_back({Action::Kind::Send, reply_to(f, dock_report(id, identity, child))});
            } else if (id == report::kDeviceInfo) {
                // The open-source client probes 0x80 (as GetReport) on every socket: silence = deck. The
                // Stream Deck app asks as a Write and gets the deck's own VID/PID.
                if (f.op == HidOp::Write) {
                    std::vector<uint8_t> p(16, 0);
                    p[0] = 0x03;
                    p[1] = 0x80;
                    p[12] = static_cast<uint8_t>(child.vid);
                    p[13] = static_cast<uint8_t>(child.vid >> 8);
                    p[14] = static_cast<uint8_t>(child.pid);
                    p[15] = static_cast<uint8_t>(child.pid >> 8);
                    out.push_back({Action::Kind::Send, reply_to(f, std::move(p))});
                }
            } else {
                // Any other feature query is the deck's own: read it over USB.
                out.push_back({Action::Kind::UsbGetFeature, f, id});
            }
            return;
        }
        auto payload = dock_report(id, identity, child, arg);
        if (payload.empty()) {
            out.push_back({Action::Kind::Unknown, f});
            return;
        }
        out.push_back({Action::Kind::Send, reply_to(f, std::move(payload))});
        if (id == report::kWatchdog && !hotplug_sent_) {
            hotplug_sent_ = true;
            if (child.attached) out.push_back({Action::Kind::Send, plug_event(child)});
        }
        return;
    }

    if (!verbatim) {
        if (port_ == Port::Dock && f.op == HidOp::SendReport && f.payload.size() >= 2 && f.payload[0] == 0x03 &&
            f.payload[1] == 0x08) {
            return;  // brightness on the dock itself: accepted, nothing to do
        }
        if (port_ == Port::Child && !f.payload.empty()) {
            // The Stream Deck app drives the deck on its port without the VERBATIM flag.
            if (f.op == HidOp::Write) return out.push_back({Action::Kind::UsbWrite, f});
            if (f.op == HidOp::SendReport) return out.push_back({Action::Kind::UsbSetFeature, f});
        }
        out.push_back({Action::Kind::Unknown, f});
        return;
    }

    if (f.payload.empty()) {
        out.push_back({Action::Kind::Unknown, f});
        return;
    }
    // Verbatim = for the deck (the open-source client, on either port).
    switch (f.op) {
        case HidOp::Write:
            out.push_back({Action::Kind::UsbWrite, f});
            break;
        case HidOp::SendReport:
            out.push_back({Action::Kind::UsbSetFeature, f});
            break;
        case HidOp::GetReport:
            out.push_back({Action::Kind::UsbGetFeature, f, f.payload[0]});
            break;
        default:
            out.push_back({Action::Kind::Unknown, f});
    }
}

Frame Session::input_frame(const uint8_t* report, size_t len) {
    // Without VERBATIM, as OpenNetDeck sends it to the Stream Deck app (the open-source client accepts both).
    Frame f;
    f.flags = flags::kNone;
    f.op = HidOp::Write;
    f.payload.assign(report, report + len);
    return f;
}

Frame Session::feature_result(const Action& request, std::vector<uint8_t> report) {
    const Frame& req = request.frame;
    if ((req.flags & flags::kVerbatim) != 0) {
        Frame f;
        f.flags = flags::kVerbatim | flags::kResult;
        f.op = HidOp::GetReport;
        f.message_id = req.message_id;
        f.payload = std::move(report);
        return f;
    }
    if (req.payload.size() >= kReportSize && report.size() < kReportSize) report.resize(kReportSize, 0);
    Frame f;
    f.flags = flags::kResult;
    f.op = req.op;
    f.message_id = req.message_id;
    f.payload = std::move(report);
    return f;
}

Frame Session::plug_event(const ChildInfo& child) {
    Frame f;
    f.flags = flags::kNone;
    f.op = HidOp::Write;
    f.payload = device2_info(child, true);
    return f;
}

}  // namespace cora
