#include "deck_ui/deck.hpp"

#include <memory>
#include <mutex>
#include <vector>

#include "driver/jpeg_encode.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb_bridge.hpp"

namespace deck_ui {
namespace {

constexpr char TAG[] = "deck_ui";
constexpr int kBrightness = 70;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

struct Pending {
    bool active = false;
    uint32_t id = 0;
    bool answered = false;
    bool yes = false;
    bool host_was_connected = false;
};

Callbacks g_cb;
std::mutex g_mutex;  // guards everything below
std::unique_ptr<Ui> g_ui;
const Model* g_model = nullptr;
NetInfo g_net;
Hotspot g_hotspot;
bool g_host = false;
std::vector<bool> g_keys;
std::vector<uint32_t> g_drawn;  // per-key image hash; 0 = must draw
std::vector<std::vector<uint8_t>> g_images;  // last image sent per key (for the web preview)
bool g_brightness_set = false;
Pending g_prompt;
uint32_t g_next_prompt = 1;
QueueHandle_t g_events = nullptr;
SemaphoreHandle_t g_answer = nullptr;
SemaphoreHandle_t g_confirm_lock = nullptr;
TaskHandle_t g_task = nullptr;

jpeg_encoder_handle_t g_jpeg = nullptr;
uint8_t* g_jpeg_in = nullptr;
uint8_t* g_jpeg_out = nullptr;
size_t g_jpeg_in_cap = 0, g_jpeg_out_cap = 0;

bool owns_locked() { return g_model && (!g_host || g_prompt.active); }

uint32_t fnv(const std::vector<uint8_t>& d) {
    uint32_t h = 2166136261u;
    for (uint8_t b : d) h = (h ^ b) * 16777619u;
    return h | 1;
}

void wake() {
    if (g_task) xTaskNotifyGive(g_task);
}

bool encode(const Model& m, const Canvas& key, std::vector<uint8_t>& out) {
    std::vector<uint8_t> px = transform(m, key);
    if (m.format == Model::Format::Bmp) {
        out = bmp(m, px);
        return true;
    }
    if (!g_jpeg) {
        jpeg_encode_engine_cfg_t ecfg{};
        ecfg.timeout_ms = 100;
        if (jpeg_new_encoder_engine(&ecfg, &g_jpeg) != ESP_OK) return false;
        jpeg_encode_memory_alloc_cfg_t in{JPEG_ENC_ALLOC_INPUT_BUFFER}, outc{JPEG_ENC_ALLOC_OUTPUT_BUFFER};
        g_jpeg_in = static_cast<uint8_t*>(jpeg_alloc_encoder_mem(96 * 96 * 3, &in, &g_jpeg_in_cap));
        g_jpeg_out = static_cast<uint8_t*>(jpeg_alloc_encoder_mem(32 * 1024, &outc, &g_jpeg_out_cap));
        if (!g_jpeg_in || !g_jpeg_out) return false;
    }
    // The encoder's "RGB888" input is BGR24 in memory.
    const size_t n = px.size();
    if (n > g_jpeg_in_cap) return false;
    for (size_t i = 0; i < n; i += 3) {
        g_jpeg_in[i] = px[i + 2];
        g_jpeg_in[i + 1] = px[i + 1];
        g_jpeg_in[i + 2] = px[i];
    }
    jpeg_encode_cfg_t cfg{};
    cfg.width = static_cast<uint32_t>(m.key_px);
    cfg.height = static_cast<uint32_t>(m.key_px);
    cfg.src_type = JPEG_ENCODE_IN_FORMAT_RGB888;
    cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV444;
    cfg.image_quality = 90;
    uint32_t size = 0;
    if (jpeg_encoder_process(g_jpeg, &cfg, g_jpeg_in, static_cast<uint32_t>(n), g_jpeg_out,
                             static_cast<uint32_t>(g_jpeg_out_cap), &size) != ESP_OK)
        return false;
    out.assign(g_jpeg_out, g_jpeg_out + size);
    return true;
}

// Draws every key whose image changed. Rendering happens under the lock; USB writes outside it.
void draw() {
    const Model* m;
    std::vector<std::pair<int, Canvas>> todo;
    bool set_brightness = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!owns_locked() || !g_ui) return;
        m = g_model;
        set_brightness = !g_brightness_set;
        g_brightness_set = true;
        for (int k = 0; k < m->keys(); ++k) {
            Canvas c(m->key_px, m->key_px);
            g_ui->render(k, c);
            const uint32_t h = fnv(c.rgb());
            if (h != g_drawn[k]) {
                g_drawn[k] = h;
                todo.emplace_back(k, std::move(c));
            }
        }
    }
    if (set_brightness) {
        auto b = brightness_report(*m, kBrightness);
        usb_bridge::set_feature(b.data(), b.size());
    }
    std::vector<uint8_t> img;
    for (auto& [k, c] : todo) {
        if (!encode(*m, c, img)) {
            ESP_LOGW(TAG, "key %d: encode failed", k);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (k < static_cast<int>(g_images.size())) g_images[k] = img;
        }
        for (const auto& r : image_reports(*m, k, img)) {
            if (!usb_bridge::write(r.data(), r.size(), pdMS_TO_TICKS(500))) {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (k < static_cast<int>(g_drawn.size())) g_drawn[k] = 0;  // try again next time
                break;
            }
        }
    }
}

void task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
        UiEvent ev;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_ui) ev = g_ui->on_tick(now_ms());
        }
        if (ev.kind != UiEvent::Kind::None) xQueueSend(g_events, &ev, 0);
        while (xQueueReceive(g_events, &ev, 0) == pdTRUE) {
            if (ev.kind == UiEvent::Kind::PromptAnswer) {
                bool replug = false;
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (g_prompt.active && g_prompt.id == ev.prompt_id) {
                        g_prompt.answered = true;
                        g_prompt.yes = ev.yes;
                        g_prompt.active = false;
                        replug = g_host && g_prompt.host_was_connected;
                        if (replug) std::fill(g_drawn.begin(), g_drawn.end(), 0);
                    }
                }
                xSemaphoreGive(g_answer);
                if (replug && g_cb.on_replug) g_cb.on_replug();
            } else if (g_cb.on_event) {
                g_cb.on_event(ev);
            }
        }
        draw();
    }
}

}  // namespace

void start(const Callbacks& cb) {
    g_cb = cb;
    g_events = xQueueCreate(8, sizeof(UiEvent));
    g_answer = xSemaphoreCreateBinary();
    g_confirm_lock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(task, "deck_ui", 8192, nullptr, 5, &g_task, 1);
}

void deck_attached(uint16_t pid) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_model = find_model(pid);
    if (!g_model) {
        ESP_LOGI(TAG, "deck %04x: no dock screens for this model", pid);
        g_ui.reset();
        return;
    }
    g_ui = std::make_unique<Ui>(*g_model);
    g_ui->set_net(g_net);
    g_ui->set_hotspot(g_hotspot);
    g_keys.assign(g_model->keys(), false);
    g_drawn.assign(g_model->keys(), 0);
    g_images.assign(g_model->keys(), {});
    g_brightness_set = false;
    ESP_LOGI(TAG, "%s: %d keys", g_model->name, g_model->keys());
    wake();
}

void deck_detached() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_model = nullptr;
    g_ui.reset();
    if (g_prompt.active) {
        g_prompt.active = false;
        g_prompt.answered = true;
        g_prompt.yes = false;
        xSemaphoreGive(g_answer);
    }
}

void set_host(bool connected) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_host = connected;
    // When the host leaves, our screens must be drawn again over whatever it left on the keys.
    if (!connected) {
        std::fill(g_drawn.begin(), g_drawn.end(), 0);
        g_brightness_set = false;
    }
    std::fill(g_keys.begin(), g_keys.end(), false);
    wake();
}

bool owns_deck() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return owns_locked();
}

void input(const uint8_t* report, size_t len) {
    std::vector<UiEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_ui || !g_model) return;
        auto keys = key_states(*g_model, report, len);
        if (keys.size() != g_keys.size()) return;
        const uint32_t t = now_ms();
        for (size_t k = 0; k < keys.size(); ++k) {
            if (keys[k] == g_keys[k]) continue;
            g_keys[k] = keys[k];
            UiEvent ev = g_ui->on_key(static_cast<int>(k), keys[k], t);
            if (ev.kind != UiEvent::Kind::None) events.push_back(ev);
        }
    }
    for (const auto& ev : events) xQueueSend(g_events, &ev, 0);
    wake();
}

void set_net(const NetInfo& n) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_net = n;
    if (g_ui) g_ui->set_net(n);
    wake();
}

void set_hotspot(const Hotspot& h) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hotspot = h;
    if (g_ui) g_ui->set_hotspot(h);
    wake();
}

bool confirm(const std::string& question) {
    xSemaphoreTake(g_confirm_lock, portMAX_DELAY);  // one prompt at a time
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_ui) {
            xSemaphoreGive(g_confirm_lock);
            return false;
        }
        xSemaphoreTake(g_answer, 0);
        g_prompt = Pending{};
        g_prompt.active = true;
        g_prompt.id = g_next_prompt++;
        g_prompt.host_was_connected = g_host;
        if (g_host) {
            // We take the deck from the host: draw every key fresh.
            std::fill(g_drawn.begin(), g_drawn.end(), 0);
            g_brightness_set = false;
        }
        g_ui->prompt(g_prompt.id, question, now_ms());
        ESP_LOGI(TAG, "asking on the deck: %s", question.c_str());
    }
    wake();
    const bool got = xSemaphoreTake(g_answer, pdMS_TO_TICKS(Ui::kPromptTimeoutMs + 2000)) == pdTRUE;
    bool yes;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        yes = got && g_prompt.answered && g_prompt.yes;
        g_prompt.active = false;
    }
    ESP_LOGI(TAG, "answer: %s", yes ? "yes" : "no");
    xSemaphoreGive(g_confirm_lock);
    return yes;
}

std::vector<uint8_t> key_image(int key, std::string& content_type) {
    std::lock_guard<std::mutex> lock(g_mutex);
    content_type = g_model && g_model->format == Model::Format::Bmp ? "image/bmp" : "image/jpeg";
    if (key < 0 || key >= static_cast<int>(g_images.size())) return {};
    return g_images[key];
}

}  // namespace deck_ui
