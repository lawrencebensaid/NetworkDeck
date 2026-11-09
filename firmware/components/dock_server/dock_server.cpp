#include "dock_server.hpp"

#include <atomic>
#include <cerrno>
#include <deque>
#include <mutex>

#include "cora/session.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace dock_server {
namespace {

constexpr char TAG[] = "dock";

struct Conn {
    Conn(int fd_, cora::Port port_, uint32_t ip_, uint8_t conn_no)
        : fd(fd_), port(port_), peer_ip(ip_), session(port_, conn_no, now_ms()) {
        send_mutex = xSemaphoreCreateMutex();
    }
    ~Conn() { vSemaphoreDelete(send_mutex); }
    static uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

    int fd;
    cora::Port port;
    uint32_t peer_ip;
    cora::Session session;
    SemaphoreHandle_t send_mutex;
};

struct Slot {
    uint32_t ip = 0;
    Conn* dock = nullptr;
    Conn* child = nullptr;
};

Config g_config;
std::mutex g_mutex;  // guards g_slot and g_child, and keeps Conns alive while others send on them
Slot g_slot;
cora::ChildInfo g_child;
std::atomic<uint8_t> g_conn_no{0};
std::atomic<bool> g_paused{false};

// Throughput counters, logged once a second while traffic flows (debug level: esp_log_level_set("dock", ESP_LOG_DEBUG)).
std::atomic<uint32_t> g_rx_bytes{0}, g_usb_reports{0}, g_usb_wait_us{0}, g_recv_calls{0};
void perf_tick() {
    static int64_t last = 0;
    const int64_t now = esp_timer_get_time();
    if (now - last < 1000000) return;
    last = now;
    const uint32_t rx = g_rx_bytes.exchange(0), rep = g_usb_reports.exchange(0), wait = g_usb_wait_us.exchange(0),
                   calls = g_recv_calls.exchange(0);
    if (rx || rep) ESP_LOGD(TAG, "perf: %u KB/s in (%u recv), %u reports/s to USB, %u ms waiting on USB", rx / 1024, calls, rep, wait / 1000);
}

std::mutex g_trace_mutex;
std::deque<std::string> g_trace;
constexpr size_t kTraceLines = 200;

void trace_add(const char* what, const cora::Frame& f) {
    std::string line = std::to_string(static_cast<unsigned>(esp_timer_get_time() / 1000)) + " " + what + " " + cora::describe(f);
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    g_trace.push_back(std::move(line));
    if (g_trace.size() > kTraceLines) g_trace.pop_front();
}

const char* port_name(cora::Port p) { return p == cora::Port::Dock ? "dock" : "deck"; }

std::string ip_str(uint32_t ip) {
    in_addr a;
    a.s_addr = ip;
    return inet_ntoa(a);
}

bool send_frame(Conn* c, const cora::Frame& f) {
    const auto bytes = cora::encode(f);
    xSemaphoreTake(c->send_mutex, portMAX_DELAY);
    size_t off = 0;
    bool ok = true;
    while (off < bytes.size()) {
        int n = send(c->fd, bytes.data() + off, bytes.size() - off, 0);
        if (n <= 0) {
            ok = false;
            break;
        }
        off += static_cast<size_t>(n);
    }
    xSemaphoreGive(c->send_mutex);
    return ok;
}

cora::DockIdentity identity_snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_config.identity;
}

cora::ChildInfo child_snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_child;
}

// Returns false when the connection should close.
bool execute(Conn* c, const cora::Action& a) {
    using K = cora::Action::Kind;
    switch (a.kind) {
        case K::Send:
            if (!(a.frame.payload.size() >= 2 && a.frame.payload[0] == 0x01 && a.frame.payload[1] == 0x0A)) {  // not keepalives
                ESP_LOGI(TAG, "%s → %s", port_name(c->port), cora::describe(a.frame).c_str());
                trace_add(c->port == cora::Port::Dock ? "dock >" : "deck >", a.frame);
            }
            return send_frame(c, a.frame);
        case K::UsbWrite: {
            if (g_paused) return true;  // the dock is showing a prompt; the host redraws after the replug
            // Blocking here stops us reading the socket: TCP then slows the host down.
            const int64_t t0 = esp_timer_get_time();
            ++g_usb_reports;
            if (g_config.on_host_output) g_config.on_host_output(a.frame.payload.data(), a.frame.payload.size());
            while (!usb_bridge::write(a.frame.payload.data(), a.frame.payload.size(), pdMS_TO_TICKS(200))) {
                if (!usb_bridge::attached()) return true;  // deck gone: drop, the plug event tells the host
                for (const auto& t : c->session.on_tick(Conn::now_ms()))
                    if (!send_frame(c, t.frame)) return false;
            }
            g_usb_wait_us += static_cast<uint32_t>(esp_timer_get_time() - t0);
            return true;
        }
        case K::UsbSetFeature:
            if (g_paused) return true;
            ESP_LOGI(TAG, "deck ← set %s", cora::describe(a.frame).c_str());
            if (g_config.on_host_feature) g_config.on_host_feature(a.frame.payload.data(), a.frame.payload.size());
            usb_bridge::set_feature(a.frame.payload.data(), a.frame.payload.size());
            return true;
        case K::UsbGetFeature: {
            std::vector<uint8_t> report;
            const uint8_t id = a.report_id;
            if (usb_bridge::get_feature(id, report) != ESP_OK) return true;  // host times out on its own
            auto reply = cora::Session::feature_result(a, std::move(report));
            ESP_LOGI(TAG, "deck get 0x%02x → %s", id, cora::describe(reply).c_str());
            return send_frame(c, reply);
        }
        case K::Unknown:
            ESP_LOGW(TAG, "%s ? %s", port_name(c->port), cora::describe(a.frame).c_str());
            trace_add(c->port == cora::Port::Dock ? "dock ?" : "deck ?", a.frame);
            return true;
    }
    return true;
}

void conn_task(void* arg) {
    Conn* c = static_cast<Conn*>(arg);
    const int one = 1;
    setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    timeval rcv{0, 50 * 1000};
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));
    timeval snd{2, 0};
    setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof(snd));
    const int rcvbuf = 65534;
    setsockopt(c->fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    ESP_LOGI(TAG, "%s connection #%u from %s", port_name(c->port), c->session.conn_no(), ip_str(c->peer_ip).c_str());
    bool alive = send_frame(c, c->session.greeting(Conn::now_ms()));

    static constexpr size_t kBuf = 16384;
    uint8_t* buf = static_cast<uint8_t*>(malloc(kBuf));
    while (alive && buf) {
        int n = recv(c->fd, buf, kBuf, 0);
        const uint32_t now = Conn::now_ms();
        perf_tick();
        if (n > 0) {
            g_rx_bytes += static_cast<uint32_t>(n);
            ++g_recv_calls;
            for (const auto& a : c->session.on_bytes(buf, static_cast<size_t>(n), identity_snapshot(), child_snapshot(), now)) {
                if (!(alive = execute(c, a))) break;
            }
        } else if (n == 0) {
            ESP_LOGI(TAG, "%s #%u closed by host", port_name(c->port), c->session.conn_no());
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGI(TAG, "%s #%u socket error %d", port_name(c->port), c->session.conn_no(), errno);
            break;
        }
        for (const auto& a : c->session.on_tick(now)) {
            if (!(alive = execute(c, a))) break;
        }
        if (alive && c->session.timed_out(now)) {
            ESP_LOGW(TAG, "%s #%u: host silent for %u ms", port_name(c->port), c->session.conn_no(),
                     static_cast<unsigned>(cora::kHostTimeoutMs));
            break;
        }
    }
    free(buf);

    bool host_gone = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_slot.dock == c) {
            g_slot.dock = nullptr;
            if (g_slot.child) shutdown(g_slot.child->fd, SHUT_RDWR);  // the host is gone: drop its deck socket too
        }
        if (g_slot.child == c) g_slot.child = nullptr;
        if (g_slot.ip && !g_slot.dock && !g_slot.child) {
            g_slot.ip = 0;
            host_gone = true;
        }
        close(c->fd);
    }
    ESP_LOGI(TAG, "%s #%u ended", port_name(c->port), c->session.conn_no());
    delete c;
    if (host_gone) {
        ESP_LOGI(TAG, "host released the dock");
        if (g_config.on_host_change) g_config.on_host_change(false);
    }
    vTaskDelete(nullptr);
}

void listener_task(void* arg) {
    const auto port = static_cast<cora::Port>(reinterpret_cast<uintptr_t>(arg));
    const uint16_t tcp_port = port == cora::Port::Dock ? cora::kDockPort : cora::kChildPort;

    int lfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(tcp_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(lfd, 2) != 0) {
        ESP_LOGE(TAG, "listen on %u failed: %d", tcp_port, errno);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "listening for %s connections on %u", port_name(port), tcp_port);

    for (;;) {
        sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        int fd = accept(lfd, reinterpret_cast<sockaddr*>(&peer), &plen);
        if (fd < 0) continue;
        const uint32_t ip = peer.sin_addr.s_addr;

        Conn* c = nullptr;
        bool host_new = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            const bool other_host = g_slot.ip && g_slot.ip != ip;
            if (port == cora::Port::Dock && !other_host) {
                c = new Conn(fd, port, ip, ++g_conn_no);
                if (g_slot.dock) shutdown(g_slot.dock->fd, SHUT_RDWR);  // same host reconnecting
                host_new = g_slot.ip == 0;
                g_slot.ip = ip;
                g_slot.dock = c;
            } else if (port == cora::Port::Child && g_slot.ip == ip && g_child.attached) {
                c = new Conn(fd, port, ip, ++g_conn_no);
                if (g_slot.child) shutdown(g_slot.child->fd, SHUT_RDWR);
                g_slot.child = c;
            }
        }
        if (!c) {
            ESP_LOGW(TAG, "refused %s connection from %s", port_name(port), ip_str(ip).c_str());
            close(fd);
            continue;
        }
        if (host_new && g_config.on_host_change) g_config.on_host_change(true);
        if (xTaskCreatePinnedToCore(conn_task, port == cora::Port::Dock ? "dock_conn" : "deck_conn", 6144, c, 8,
                                    nullptr, 1) != pdPASS) {
            ESP_LOGE(TAG, "no memory for connection task");
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_slot.dock == c) g_slot.dock = nullptr;
            if (g_slot.child == c) g_slot.child = nullptr;
            close(fd);
            delete c;
        }
    }
}

void send_plug_event_locked() {
    if (g_slot.dock) send_frame(g_slot.dock, cora::Session::plug_event(g_child));
}

}  // namespace

void start(const Config& config) {
    g_config = config;
    xTaskCreatePinnedToCore(listener_task, "dock_listen", 4096, reinterpret_cast<void*>(cora::Port::Dock), 7, nullptr, 1);
    xTaskCreatePinnedToCore(listener_task, "deck_listen", 4096, reinterpret_cast<void*>(cora::Port::Child), 7, nullptr, 1);
}

bool host_connected() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_slot.ip != 0;
}

void deck_attached(const usb_bridge::DeckInfo& info) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_child.attached = true;
    g_child.vid = info.vid;
    g_child.pid = info.pid;
    g_child.serial = info.serial;
    g_child.name = info.product;
    g_child.bcd_device = info.bcd_device ? info.bcd_device : 0x0100;
    g_child.tcp_port = cora::kChildPort;
    send_plug_event_locked();
}

void deck_detached() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_child = cora::ChildInfo{};
    send_plug_event_locked();
    if (g_slot.child) shutdown(g_slot.child->fd, SHUT_RDWR);
}

void deck_input(const uint8_t* report, size_t len) {
    if (g_paused) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    // The open-source client reads keys on the child connection; the official app on the dock one.
    Conn* to = g_slot.child ? g_slot.child : g_slot.dock;
    if (to) send_frame(to, cora::Session::input_frame(report, len));
}

void set_paused(bool paused) { g_paused = paused; }

void set_network(const std::array<uint8_t, 4>& ip, const std::array<uint8_t, 4>& netmask) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_config.identity.ip = ip;
    g_config.identity.netmask = netmask;
}

void replug() {
    cora::ChildInfo attached;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_child.attached) return;
        attached = g_child;
        g_child = cora::ChildInfo{};
        send_plug_event_locked();
        if (g_slot.child) shutdown(g_slot.child->fd, SHUT_RDWR);
    }
    ESP_LOGI(TAG, "virtual replug");
    vTaskDelay(pdMS_TO_TICKS(500));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_child = attached;
    send_plug_event_locked();
}

std::string trace() {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    std::string out;
    for (const auto& l : g_trace) out += l + "\n";
    return out;
}

}  // namespace dock_server
