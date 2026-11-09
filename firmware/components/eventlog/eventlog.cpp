// Event log: see eventlog.hpp.

#include "eventlog.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

#include "esp_flash.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace eventlog {
namespace {

constexpr char TAG[] = "eventlog";
constexpr size_t kOffset = 0xF00000;
constexpr size_t kSize = 0x100000;
constexpr size_t kSector = 4096;
constexpr uint16_t kMagic = 0xE7A1;
constexpr time_t kClockSet = 1700000000;  // anything earlier is the clock before SNTP set it

struct Record {
    uint16_t magic;
    uint8_t cat;
    uint8_t level;
    uint32_t seq;
    uint32_t epoch;   // 0: before the clock was set
    uint32_t uptime;  // seconds since this boot started
    uint16_t boot;
    uint16_t check;   // over everything but itself
    char text[108];
};
static_assert(sizeof(Record) == 128, "a record is 128 bytes");
constexpr size_t kPerSector = kSector / sizeof(Record);
constexpr size_t kCapacity = kSize / sizeof(Record);

const esp_partition_t* g_part = nullptr;
std::mutex g_mutex;  // g_head, g_seq, the flash
size_t g_head = 0;   // next slot to write
uint32_t g_seq = 0;  // last sequence number written
uint16_t g_boot = 0;
QueueHandle_t g_queue = nullptr;

uint16_t checksum(const Record& r) {
    const auto* p = reinterpret_cast<const uint8_t*>(&r);
    uint16_t h = 0x1D0F;
    for (size_t i = 0; i < sizeof(Record); ++i) {
        if (i == offsetof(Record, check) || i == offsetof(Record, check) + 1) continue;
        h = static_cast<uint16_t>((h << 5) ^ (h >> 11) ^ p[i]);
    }
    return h;
}

bool read(size_t slot, Record& r) {
    if (esp_partition_read(g_part, slot * sizeof(Record), &r, sizeof(r)) != ESP_OK) return false;
    return r.magic == kMagic && r.check == checksum(r);
}

// The newest record: the sector whose first record has the highest sequence number, then the last
// valid record in it.
void find_head() {
    uint32_t best = 0;
    size_t best_sector = 0;
    bool any = false;
    Record r;
    for (size_t s = 0; s < kSize / kSector; ++s) {
        if (read(s * kPerSector, r) && (!any || r.seq > best)) {
            best = r.seq;
            best_sector = s;
            any = true;
        }
    }
    if (!any) {
        g_head = 0;
        g_seq = 0;
        g_boot = 0;
        return;
    }
    size_t slot = best_sector * kPerSector;
    g_seq = best;
    g_boot = r.boot;
    read(slot, r);
    g_boot = r.boot;
    for (size_t i = 1; i < kPerSector; ++i) {
        if (!read(slot + i, r) || r.seq != g_seq + 1) break;
        g_seq = r.seq;
        g_boot = r.boot;
    }
    g_head = (slot + (g_seq - best) + 1) % kCapacity;
}

void write_locked(Record& r) {
    if (g_head % kPerSector == 0) esp_partition_erase_range(g_part, g_head * sizeof(Record), kSector);
    r.seq = ++g_seq;
    r.check = checksum(r);
    esp_partition_write(g_part, g_head * sizeof(Record), &r, sizeof(r));
    g_head = (g_head + 1) % kCapacity;
}

void writer(void*) {
    Record r;
    for (;;) {
        if (xQueueReceive(g_queue, &r, portMAX_DELAY) != pdTRUE) continue;
        std::lock_guard<std::mutex> lock(g_mutex);
        write_locked(r);
    }
}

const char* level_name(uint8_t l) { return l == 2 ? "error" : l == 1 ? "warn" : "info"; }

void escape(std::string& out, const char* s) {
    for (; *s; ++s) {
        const char c = *s;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            out += c;
        }
    }
}

}  // namespace

const char* cat_name(Cat c) {
    static const char* kNames[] = {"system", "update", "network", "connection", "deck", "security", "matter"};
    const auto i = static_cast<size_t>(c);
    return i < sizeof(kNames) / sizeof(kNames[0]) ? kNames[i] : "system";
}

void start() {
    if (g_queue) return;
    g_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "eventlog");
    uint32_t flash_size = 0;
    esp_flash_get_size(esp_flash_default_chip, &flash_size);
    if (!g_part && flash_size >= kOffset + kSize) {
        // An older partition table (a P4 dock updated over the air): the same place, registered here.
        const esp_err_t err = esp_partition_register_external(esp_flash_default_chip, kOffset, kSize, "eventlog",
                                                              ESP_PARTITION_TYPE_DATA,
                                                              static_cast<esp_partition_subtype_t>(0x40), &g_part);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "no room for the event log: %s", esp_err_to_name(err));
            g_part = nullptr;
            return;
        }
    }
    if (!g_part) {
        ESP_LOGE(TAG, "no event log partition");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        find_head();
        ++g_boot;
    }
    g_queue = xQueueCreate(24, sizeof(Record));
    xTaskCreatePinnedToCore(writer, "eventlog", 3072, nullptr, 2, nullptr, 0);
    ESP_LOGI(TAG, "start %u, %u records so far", g_boot, static_cast<unsigned>(g_seq));
}

void add(Cat cat, Level level, const char* fmt, ...) {
    Record r{};
    r.magic = kMagic;
    r.cat = static_cast<uint8_t>(cat);
    r.level = static_cast<uint8_t>(level);
    const time_t now = time(nullptr);
    r.epoch = now >= kClockSet ? static_cast<uint32_t>(now) : 0;
    r.uptime = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    r.boot = g_boot;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(r.text, sizeof(r.text), fmt, ap);
    va_end(ap);
    ESP_LOGI(TAG, "[%s] %s", cat_name(cat), r.text);
    if (!g_queue || xQueueSend(g_queue, &r, 0) != pdTRUE) ESP_LOGW(TAG, "dropped: %s", r.text);
}

std::string json(uint32_t before, size_t limit) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const time_t now = time(nullptr);
    const uint32_t kept = std::min<uint32_t>(g_seq, static_cast<uint32_t>(kCapacity - kPerSector));
    char head[200];
    std::snprintf(head, sizeof(head), "{\"capacity\":%u,\"kept\":%u,\"boot\":%u,\"now\":%u,\"uptime\":%u,\"records\":[",
                  static_cast<unsigned>(kCapacity - kPerSector), static_cast<unsigned>(kept), g_boot,
                  static_cast<unsigned>(now >= kClockSet ? now : 0), static_cast<unsigned>(esp_timer_get_time() / 1000000));
    std::string j = head;
    if (!g_part) return j + "]}";
    limit = std::min<size_t>(limit, 500);
    size_t slot = (g_head + kCapacity - 1) % kCapacity;
    size_t n = 0;
    Record r;
    for (size_t i = 0; i < kept && n < limit; ++i, slot = (slot + kCapacity - 1) % kCapacity) {
        if (!read(slot, r)) break;
        if (before && r.seq >= before) continue;
        char b[96];
        std::snprintf(b, sizeof(b), "%s{\"s\":%u,\"b\":%u,\"t\":%u,\"u\":%u,\"c\":\"%s\",\"l\":\"%s\",\"m\":\"", n ? "," : "",
                      static_cast<unsigned>(r.seq), r.boot, static_cast<unsigned>(r.epoch), static_cast<unsigned>(r.uptime),
                      cat_name(static_cast<Cat>(r.cat)), level_name(r.level));
        j += b;
        r.text[sizeof(r.text) - 1] = '\0';
        escape(j, r.text);
        j += "\"}";
        ++n;
    }
    return j + "]}";
}

}  // namespace eventlog
