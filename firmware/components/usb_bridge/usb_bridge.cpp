#include "usb_bridge.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hidparse/hid_descriptor.hpp"
#include "usb/usb_host.h"


namespace usb_bridge {
namespace {

constexpr char TAG[] = "usb";
constexpr uint16_t kElgatoVid = 0x0FD9;
constexpr size_t kMaxOut = 1024;
constexpr size_t kOutQueueDepth = 16;
constexpr size_t kCtrlMax = 1024;
constexpr uint8_t kHidClass = 0x03;
constexpr uint8_t kHidDescType = 0x21;
constexpr uint8_t kReportDescType = 0x22;
constexpr uint8_t kFeature = 0x03;
constexpr TickType_t kCtrlTimeout = pdMS_TO_TICKS(1000);

struct OutMsg {
    uint16_t len;
    uint8_t data[kMaxOut];
};

struct Device {
    usb_device_handle_t hdl = nullptr;
    uint8_t iface = 0;
    uint8_t ep_in = 0, ep_out = 0;
    uint16_t mps_in = 0, mps_out = 0;
    uint16_t report_desc_len = 0;
    usb_transfer_t* xin = nullptr;
    usb_transfer_t* xout = nullptr;
    usb_transfer_t* xctrl = nullptr;
    hidparse::ReportSizes sizes;
    DeckInfo info;
    bool claimed = false;
};

Callbacks g_cb;
usb_host_client_handle_t g_client = nullptr;
TaskHandle_t g_client_task = nullptr;
QueueHandle_t g_out_q = nullptr;
SemaphoreHandle_t g_ctrl_mutex = nullptr;
SemaphoreHandle_t g_ctrl_done = nullptr;
std::mutex g_info_mutex;

Device g_dev;
std::atomic<bool> g_attached{false};
std::atomic<bool> g_out_busy{false};
std::atomic<int> g_inflight{0};
std::atomic<uint8_t> g_new_addr{0};
std::atomic<bool> g_gone{false};
std::atomic<TickType_t> g_out_since{0};  // when the in-flight OUT transfer was submitted

// ── transfers ──────────────────────────────────────────────────────────────

void in_cb(usb_transfer_t* x) {
    if (x->status == USB_TRANSFER_STATUS_COMPLETED && g_attached && x->actual_num_bytes > 0 && g_cb.on_input) {
        g_cb.on_input(x->data_buffer, x->actual_num_bytes);
    }
    if (g_attached && !g_gone && x->status != USB_TRANSFER_STATUS_NO_DEVICE &&
        x->status != USB_TRANSFER_STATUS_CANCELED) {
        if (usb_host_transfer_submit(x) == ESP_OK) return;
    }
    --g_inflight;
}

void kick_out();

void out_cb(usb_transfer_t* x) {
    // A lost image packet corrupts that key until the host redraws it, so resend a failed report a
    // few times. The data is still in the transfer buffer.
    static int retries = 0;
    if (x->status == USB_TRANSFER_STATUS_ERROR && g_attached && !g_gone && retries < 3) {
        ++retries;
        if (usb_host_transfer_submit(x) == ESP_OK) return;
    }
    if (x->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "OUT transfer status %d after %d retries", x->status, retries);
    } else if (retries) {
        ESP_LOGI(TAG, "OUT report resent %d time(s)", retries);
    }
    retries = 0;
    --g_inflight;
    g_out_busy = false;
    kick_out();
}

// Submit the next queued OUT report if none is in flight. Safe from any task and from out_cb.
void kick_out() {
    for (;;) {
        bool expected = false;
        if (!g_out_busy.compare_exchange_strong(expected, true)) return;
        OutMsg* msg = nullptr;
        static OutMsg scratch;  // only touched by whoever holds g_out_busy
        if (g_attached && xQueueReceive(g_out_q, &scratch, 0) == pdTRUE) msg = &scratch;
        if (msg) {
            std::memcpy(g_dev.xout->data_buffer, msg->data, msg->len);
            g_dev.xout->num_bytes = msg->len;
            ++g_inflight;
            g_out_since = xTaskGetTickCountFromISR();
            if (usb_host_transfer_submit(g_dev.xout) == ESP_OK) return;
            --g_inflight;
            ESP_LOGW(TAG, "OUT submit failed");
        }
        g_out_busy = false;
        // An item may have arrived between the empty receive and clearing the flag.
        if (!g_attached || uxQueueMessagesWaiting(g_out_q) == 0) return;
    }
}

void ctrl_cb(usb_transfer_t*) { xSemaphoreGive(g_ctrl_done); }

// Control transfer with `len` data bytes after the setup packet. On the client task we must pump
// events ourselves, because the completion callback is delivered by usb_host_client_handle_events.
esp_err_t control(uint8_t bm, uint8_t req, uint16_t value, uint16_t index, uint16_t len, int* actual) {
    usb_transfer_t* x = g_dev.xctrl;
    auto* setup = reinterpret_cast<usb_setup_packet_t*>(x->data_buffer);
    setup->bmRequestType = bm;
    setup->bRequest = req;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = len;
    x->num_bytes = static_cast<int>(sizeof(usb_setup_packet_t) + len);
    x->device_handle = g_dev.hdl;
    x->bEndpointAddress = 0;
    x->callback = ctrl_cb;
    xSemaphoreTake(g_ctrl_done, 0);  // clear a stale give

    ++g_inflight;
    esp_err_t err = usb_host_transfer_submit_control(g_client, x);
    if (err != ESP_OK) {
        --g_inflight;
        return err;
    }

    bool done = false;
    if (xTaskGetCurrentTaskHandle() == g_client_task) {
        for (int i = 0; i < 100 && !done; ++i) {
            usb_host_client_handle_events(g_client, pdMS_TO_TICKS(10));
            done = xSemaphoreTake(g_ctrl_done, 0) == pdTRUE;
        }
    } else {
        done = xSemaphoreTake(g_ctrl_done, kCtrlTimeout) == pdTRUE;
    }
    if (!done) {
        // Leave g_inflight raised: the transfer is still owned by the stack.
        return ESP_ERR_TIMEOUT;
    }
    --g_inflight;
    if (x->status != USB_TRANSFER_STATUS_COMPLETED) return ESP_FAIL;
    if (actual) *actual = x->actual_num_bytes - static_cast<int>(sizeof(usb_setup_packet_t));
    return ESP_OK;
}

uint16_t feature_size(uint8_t id, size_t fallback) {
    auto it = g_dev.sizes.feature.find(id);
    return it != g_dev.sizes.feature.end() ? it->second : static_cast<uint16_t>(fallback);
}

// ── device lifecycle (client task only) ──────────────────────────────────

std::string utf16_to_ascii(const usb_str_desc_t* d) {
    std::string s;
    if (!d) return s;
    int chars = (d->bLength - 2) / 2;
    for (int i = 0; i < chars; ++i) {
        uint16_t c = d->wData[i];
        s.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return s;
}

bool parse_config(const usb_config_desc_t* cd) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(cd);
    const int total = cd->wTotalLength;
    bool in_hid = false, found = false;
    for (int i = 0; i + 1 < total;) {
        const uint8_t len = p[i], type = p[i + 1];
        if (len == 0) break;
        if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && i + 5 < total) {
            in_hid = !found && p[i + 5] == kHidClass;
            if (in_hid) {
                found = true;
                g_dev.iface = p[i + 2];
            }
        } else if (in_hid && type == kHidDescType && i + 8 < total) {
            g_dev.report_desc_len = static_cast<uint16_t>(p[i + 7] | (p[i + 8] << 8));
        } else if (in_hid && type == USB_B_DESCRIPTOR_TYPE_ENDPOINT && i + 5 < total) {
            const uint8_t addr = p[i + 2];
            const uint16_t mps = static_cast<uint16_t>((p[i + 4] | (p[i + 5] << 8)) & 0x7FF);
            if (addr & 0x80) {
                g_dev.ep_in = addr;
                g_dev.mps_in = mps;
            } else {
                g_dev.ep_out = addr;
                g_dev.mps_out = mps;
            }
        }
        i += len;
    }
    return found && g_dev.ep_in && g_dev.ep_out;
}

void free_transfers() {
    for (usb_transfer_t** x : {&g_dev.xin, &g_dev.xout, &g_dev.xctrl}) {
        if (*x) usb_host_transfer_free(*x);
        *x = nullptr;
    }
}

void close_device() {
    if (!g_dev.hdl) return;
    const bool was_attached = g_attached.exchange(false);
    if (was_attached && g_cb.on_detach) g_cb.on_detach();
    xQueueReset(g_out_q);

    if (g_dev.claimed) {
        usb_host_endpoint_halt(g_dev.hdl, g_dev.ep_in);
        usb_host_endpoint_flush(g_dev.hdl, g_dev.ep_in);
        usb_host_endpoint_halt(g_dev.hdl, g_dev.ep_out);
        usb_host_endpoint_flush(g_dev.hdl, g_dev.ep_out);
    }
    for (int i = 0; i < 100 && g_inflight > 0; ++i) usb_host_client_handle_events(g_client, pdMS_TO_TICKS(10));
    if (g_inflight > 0) ESP_LOGW(TAG, "%d transfers still in flight at close", g_inflight.load());

    // Control users (set/get_feature, write) check g_attached under this mutex before touching xctrl.
    xSemaphoreTake(g_ctrl_mutex, portMAX_DELAY);
    if (g_dev.claimed) usb_host_interface_release(g_client, g_dev.hdl, g_dev.iface);
    free_transfers();
    usb_host_device_close(g_client, g_dev.hdl);
    g_dev = Device{};
    xSemaphoreGive(g_ctrl_mutex);
    g_inflight = 0;
    g_out_busy = false;
    ESP_LOGI(TAG, "deck closed");
}

void open_device(uint8_t addr) {
    if (usb_host_device_open(g_client, addr, &g_dev.hdl) != ESP_OK) {
        g_dev.hdl = nullptr;
        return;
    }
    const usb_device_desc_t* dd = nullptr;
    usb_host_get_device_descriptor(g_dev.hdl, &dd);
    if (!dd || dd->idVendor != kElgatoVid) {
        ESP_LOGW(TAG, "ignoring non-Elgato device %04x:%04x", dd ? dd->idVendor : 0, dd ? dd->idProduct : 0);
        usb_host_device_close(g_client, g_dev.hdl);
        g_dev = Device{};
        return;
    }
    const usb_config_desc_t* cd = nullptr;
    usb_host_get_active_config_descriptor(g_dev.hdl, &cd);
    if (!cd || !parse_config(cd)) {
        ESP_LOGE(TAG, "%04x:%04x has no HID interface with IN and OUT endpoints", dd->idVendor, dd->idProduct);
        close_device();
        return;
    }

    usb_device_info_t dinfo{};
    usb_host_device_info(g_dev.hdl, &dinfo);

    if (usb_host_interface_claim(g_client, g_dev.hdl, g_dev.iface, 0) != ESP_OK) {
        ESP_LOGE(TAG, "interface %u claim failed", g_dev.iface);
        close_device();
        return;
    }
    g_dev.claimed = true;

    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + kCtrlMax, 0, &g_dev.xctrl) != ESP_OK) {
        close_device();
        return;
    }

    // HID report descriptor: GET_DESCRIPTOR(Report) to the interface.
    const uint16_t rd_len = std::min<uint16_t>(g_dev.report_desc_len ? g_dev.report_desc_len : 512, kCtrlMax);
    int got = 0;
    esp_err_t err = control(USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_RECIP_INTERFACE, USB_B_REQUEST_GET_DESCRIPTOR,
                            static_cast<uint16_t>(kReportDescType << 8), g_dev.iface, rd_len, &got);
    if (err == ESP_OK && got > 0) {
        g_dev.sizes = hidparse::parse(g_dev.xctrl->data_buffer + sizeof(usb_setup_packet_t), got);
    } else {
        ESP_LOGW(TAG, "report descriptor read failed (%s); using endpoint sizes", esp_err_to_name(err));
    }
    {
        // SET_IDLE(0): report only on change, no repeats. Host HID stacks send this on attach.
        const esp_err_t idle = control(USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
                                       0x0A /* SET_IDLE */, 0, g_dev.iface, 0, nullptr);
        ESP_LOGI(TAG, "SET_IDLE: %s", esp_err_to_name(idle));
    }

    auto biggest = [](const std::map<uint8_t, uint16_t>& m, uint16_t fallback) {
        uint16_t v = 0;
        for (const auto& kv : m) v = std::max(v, kv.second);
        return v ? v : fallback;
    };
    const uint16_t in_size = biggest(g_dev.sizes.input, g_dev.mps_in);
    const uint16_t out_size = std::min<uint16_t>(biggest(g_dev.sizes.output, g_dev.mps_out), kMaxOut);
    // Non-control IN transfers must be a multiple of MPS.
    const size_t in_buf = ((in_size + g_dev.mps_in - 1) / g_dev.mps_in) * g_dev.mps_in;

    if (usb_host_transfer_alloc(in_buf, 0, &g_dev.xin) != ESP_OK ||
        usb_host_transfer_alloc(kMaxOut, 0, &g_dev.xout) != ESP_OK) {
        close_device();
        return;
    }
    g_dev.xin->device_handle = g_dev.hdl;
    g_dev.xin->bEndpointAddress = g_dev.ep_in;
    g_dev.xin->num_bytes = static_cast<int>(in_buf);
    g_dev.xin->callback = in_cb;
    g_dev.xout->device_handle = g_dev.hdl;
    g_dev.xout->bEndpointAddress = g_dev.ep_out;
    g_dev.xout->callback = out_cb;

    {
        std::lock_guard<std::mutex> lock(g_info_mutex);
        g_dev.info.vid = dd->idVendor;
        g_dev.info.pid = dd->idProduct;
        g_dev.info.serial = utf16_to_ascii(dinfo.str_desc_serial_num);
        g_dev.info.input_size = in_size;
        g_dev.info.output_size = out_size;
    }

    g_attached = true;
    ++g_inflight;
    if (usb_host_transfer_submit(g_dev.xin) != ESP_OK) {
        --g_inflight;
        ESP_LOGE(TAG, "IN submit failed");
        close_device();
        return;
    }

    ESP_LOGI(TAG, "deck %04x:%04x serial %s, %s speed, iface %u, IN 0x%02x/%u (report %u), OUT 0x%02x/%u (report %u), %u feature reports",
             dd->idVendor, dd->idProduct, g_dev.info.serial.c_str(), dinfo.speed == USB_SPEED_HIGH ? "high" : "full",
             g_dev.iface, g_dev.ep_in, g_dev.mps_in, in_size, g_dev.ep_out, g_dev.mps_out, out_size,
             static_cast<unsigned>(g_dev.sizes.feature.size()));
    if (g_cb.on_attach) g_cb.on_attach(g_dev.info);
}

void client_event_cb(const usb_host_client_event_msg_t* msg, void*) {
    switch (msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            g_new_addr = msg->new_dev.address;
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            g_gone = true;
            g_attached = false;  // stop resubmitting and refuse new writes right away
            break;
        default:
            break;
    }
}

void client_task(void*) {
    usb_host_client_config_t cc{};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 5;
    cc.async.client_event_callback = client_event_cb;
    cc.async.callback_arg = nullptr;
    ESP_ERROR_CHECK(usb_host_client_register(&cc, &g_client));

    // A deck plugged in at boot can finish enumerating before we registered, and then no
    // NEW_DEV event comes: pick up whatever is already there.
    uint8_t addrs[4];
    int n = 0;
    if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &n) == ESP_OK && n > 0) g_new_addr = addrs[0];
    ESP_LOGI(TAG, "USB host ready, %d device(s) present", n);

    TickType_t last_check = xTaskGetTickCount();
    for (;;) {
        usb_host_client_handle_events(g_client, pdMS_TO_TICKS(50));
        if (xTaskGetTickCount() - last_check > pdMS_TO_TICKS(1000)) {
            last_check = xTaskGetTickCount();
            if (g_out_busy && last_check - g_out_since > pdMS_TO_TICKS(1000)) {
                ESP_LOGW(TAG, "OUT transfer in flight for %u ms, %u reports queued",
                         static_cast<unsigned>(pdTICKS_TO_MS(last_check - g_out_since)),
                         static_cast<unsigned>(uxQueueMessagesWaiting(g_out_q)));
            }
        }
        if (g_gone.exchange(false)) {
            // close_device() fires on_detach only if we were attached; DEV_GONE already cleared it.
            if (g_dev.hdl && g_cb.on_detach) g_cb.on_detach();
            close_device();
        }
        if (uint8_t addr = g_new_addr.exchange(0)) {
            if (g_dev.hdl) {
                ESP_LOGW(TAG, "second device at address %u ignored", addr);
            } else {
                open_device(addr);
            }
        }
    }
}

void lib_task(void*) {
    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

}  // namespace

// GET_REPORT on EP0 (HID 1.11 7.2.1: mandatory for every report type).
esp_err_t get_report(uint8_t type, uint8_t id, uint16_t size, std::vector<uint8_t>& out) {
    xSemaphoreTake(g_ctrl_mutex, portMAX_DELAY);
    if (!g_attached || !g_dev.xctrl) {
        xSemaphoreGive(g_ctrl_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    int got = 0;
    esp_err_t err = control(USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
                            0x01 /* GET_REPORT */, static_cast<uint16_t>((type << 8) | id), g_dev.iface, size, &got);
    if (err == ESP_OK) {
        const uint8_t* buf = g_dev.xctrl->data_buffer + sizeof(usb_setup_packet_t);
        out.assign(buf, buf + std::max(got, 0));
    }
    xSemaphoreGive(g_ctrl_mutex);
    return err;
}

esp_err_t start(const Callbacks& callbacks) {
    g_cb = callbacks;
    g_out_q = xQueueCreate(kOutQueueDepth, sizeof(OutMsg));
    g_ctrl_mutex = xSemaphoreCreateMutex();
    g_ctrl_done = xSemaphoreCreateBinary();
    if (!g_out_q || !g_ctrl_mutex || !g_ctrl_done) return ESP_ERR_NO_MEM;

    usb_host_config_t hc{};
    hc.skip_phy_setup = false;
    hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
    hc.peripheral_map = 0;  // default = the high-speed OTG port on the P4
    esp_err_t err = usb_host_install(&hc);
    if (err != ESP_OK) return err;

    xTaskCreatePinnedToCore(lib_task, "usb_lib", 4096, nullptr, 10, nullptr, 0);
    xTaskCreatePinnedToCore(client_task, "usb_client", 6144, nullptr, 9, &g_client_task, 0);
    return ESP_OK;
}

bool attached() { return g_attached; }

DeckInfo info() {
    std::lock_guard<std::mutex> lock(g_info_mutex);
    return g_dev.info;
}

bool write(const uint8_t* data, size_t len, TickType_t wait) {
    if (!g_attached || len == 0 || len > kMaxOut) return false;
    static OutMsg msg;  // xQueueSend copies; guarded so concurrent writers don't share it
    static std::mutex write_mutex;
    std::lock_guard<std::mutex> lock(write_mutex);
    msg.len = static_cast<uint16_t>(len);
    std::memcpy(msg.data, data, len);
    // Pad short reports to the deck's OUT report size: HID devices expect full reports.
    const uint16_t full = info().output_size;
    if (full > len && full <= kMaxOut) {
        std::memset(msg.data + len, 0, full - len);
        msg.len = full;
    }
    if (xQueueSend(g_out_q, &msg, wait) != pdTRUE) return false;
    kick_out();
    return true;
}

esp_err_t set_feature(const uint8_t* data, size_t len) {
    if (!g_attached || len == 0) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(g_ctrl_mutex, portMAX_DELAY);
    if (!g_attached || !g_dev.xctrl) {
        xSemaphoreGive(g_ctrl_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t id = data[0];
    const uint16_t size = std::min<uint16_t>(feature_size(id, len), kCtrlMax);
    uint8_t* buf = g_dev.xctrl->data_buffer + sizeof(usb_setup_packet_t);
    std::memset(buf, 0, size);
    std::memcpy(buf, data, std::min<size_t>(len, size));
    esp_err_t err = control(USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
                            0x09 /* SET_REPORT */, static_cast<uint16_t>((kFeature << 8) | id), g_dev.iface, size, nullptr);
    xSemaphoreGive(g_ctrl_mutex);
    if (err != ESP_OK) ESP_LOGW(TAG, "SET_REPORT 0x%02x failed: %s", id, esp_err_to_name(err));
    return err;
}

esp_err_t get_feature(uint8_t report_id, std::vector<uint8_t>& out) {
    if (!g_attached) return ESP_ERR_INVALID_STATE;
    esp_err_t err = get_report(kFeature, report_id, std::min<uint16_t>(feature_size(report_id, 32), kCtrlMax), out);
    if (err != ESP_OK) ESP_LOGW(TAG, "GET_REPORT 0x%02x failed: %s", report_id, esp_err_to_name(err));
    return err;
}

}  // namespace usb_bridge
