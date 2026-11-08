#include "dock_server.hpp"

#include <atomic>
#include <cerrno>
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
            }
            return send_frame(c, a.frame);
        case K::UsbWrite: {
            // Blocking here stops us reading the socket: TCP then slows the host down.
            while (!usb_bridge::write(a.frame.payload.data(), a.frame.payload.size(), pdMS_TO_TICKS(200))) {
                if (!usb_bridge::attached()) return true;  // deck gone: drop, the plug event tells the host
                for (const auto& t : c->session.on_tick(Conn::now_ms()))
                    if (!send_frame(c, t.frame)) return false;
            }
            return true;
        }
        case K::UsbSetFeature:
            ESP_LOGI(TAG, "deck ← set %s", cora::describe(a.frame).c_str());
            usb_bridge::set_feature(a.frame.payload.data(), a.frame.payload.size());
            return true;
        case K::UsbGetFeature: {
            std::vector<uint8_t> report;
            const uint8_t id = a.frame.payload[0];
            if (usb_bridge::get_feature(id, report) != ESP_OK) return true;  // host times out on its own
            auto reply = cora::Session::feature_result(a.frame.message_id, std::move(report));
            ESP_LOGI(TAG, "deck get 0x%02x → %s", id, cora::describe(reply).c_str());
            return send_frame(c, reply);
        }
        case K::Unknown:
            ESP_LOGW(TAG, "%s ? %s", port_name(c->port), cora::describe(a.frame).c_str());
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

    ESP_LOGI(TAG, "%s connection #%u from %s", port_name(c->port), c->session.conn_no(), ip_str(c->peer_ip).c_str());
    bool alive = send_frame(c, c->session.greeting(Conn::now_ms()));

    static constexpr size_t kBuf = 4096;
    uint8_t* buf = static_cast<uint8_t*>(malloc(kBuf));
    while (alive && buf) {
        int n = recv(c->fd, buf, kBuf, 0);
        const uint32_t now = Conn::now_ms();
        if (n > 0) {
            for (const auto& a : c->session.on_bytes(buf, static_cast<size_t>(n), g_config.identity, child_snapshot(), now)) {
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
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_slot.child) send_frame(g_slot.child, cora::Session::input_frame(report, len));
}

}  // namespace dock_server
