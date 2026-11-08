// One TCP connection's protocol state: keepalive, timeouts and routing of incoming frames.
// Pure logic. The caller owns the socket and the USB side and executes the returned actions.
#pragma once

#include <cstdint>
#include <vector>

#include "cora/frame.hpp"
#include "cora/protocol.hpp"

namespace cora {

enum class Port { Dock, Child };

inline constexpr uint32_t kKeepaliveIntervalMs = 1000;
inline constexpr uint32_t kHostTimeoutMs = 5000;

struct Action {
    enum class Kind {
        Send,           // send `frame` to the host
        UsbWrite,       // interrupt OUT: frame.payload is the output report
        UsbSetFeature,  // SET_REPORT(Feature): frame.payload is the report, starting with its id
        UsbGetFeature,  // GET_REPORT(Feature, frame.payload[0]); reply with feature_result(frame.message_id, …)
        Unknown,        // not understood: log `frame` for reverse engineering
    };
    Kind kind;
    Frame frame;
};

class Session {
public:
    Session(Port port, uint8_t conn_no, uint32_t now_ms);

    // The dock speaks first: the host picks the framing from our first bytes.
    Frame greeting(uint32_t now_ms);

    std::vector<Action> on_bytes(const uint8_t* data, size_t len, const DockIdentity& identity,
                                 const ChildInfo& child, uint32_t now_ms);
    std::vector<Action> on_tick(uint32_t now_ms);
    bool timed_out(uint32_t now_ms) const;

    Port port() const { return port_; }
    uint8_t conn_no() const { return conn_no_; }

    static Frame input_frame(const uint8_t* report, size_t len);
    static Frame feature_result(uint32_t message_id, std::vector<uint8_t> report);
    static Frame plug_event(const ChildInfo& child);

private:
    void handle(const Frame& f, const DockIdentity& identity, const ChildInfo& child, std::vector<Action>& out);
    void route(const Frame& f, const DockIdentity& identity, const ChildInfo& child, std::vector<Action>& out);
    Frame keepalive();

    Port port_;
    uint8_t conn_no_;
    Decoder decoder_;
    uint32_t last_rx_ms_;
    uint32_t last_keepalive_ms_;
};

}  // namespace cora
