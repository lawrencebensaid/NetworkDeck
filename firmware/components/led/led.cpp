// The status LED: see led.hpp.

#include "led.hpp"

#include "esp_log.h"
#include "led_strip.h"

namespace led {
namespace {

led_strip_handle_t g_strip = nullptr;
State g_state = State::Off;

}  // namespace

void start(int gpio) {
    if (gpio < 0 || g_strip) return;
    led_strip_config_t cfg{};
    cfg.strip_gpio_num = gpio;
    cfg.max_leds = 1;
    cfg.led_model = LED_MODEL_WS2812;
    cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt{};
    rmt.resolution_hz = 10 * 1000 * 1000;
    if (led_strip_new_rmt_device(&cfg, &rmt, &g_strip) != ESP_OK) {
        ESP_LOGW("led", "no LED on GPIO %d", gpio);
        g_strip = nullptr;
        return;
    }
    led_strip_clear(g_strip);
}

void set(State s) {
    if (!g_strip || s == g_state) return;
    g_state = s;
    // Dim: an LED on a desk next to the deck.
    switch (s) {
        case State::Setup: led_strip_set_pixel(g_strip, 0, 0, 0, 40); break;
        case State::Ready: led_strip_set_pixel(g_strip, 0, 0, 28, 0); break;
        case State::AppConnected: led_strip_set_pixel(g_strip, 0, 24, 0, 36); break;
        case State::Off: led_strip_clear(g_strip); return;
    }
    led_strip_refresh(g_strip);
}

}  // namespace led
