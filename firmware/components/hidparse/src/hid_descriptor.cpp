#include "hidparse/hid_descriptor.hpp"

#include <vector>

namespace hidparse {
namespace {

struct Globals {
    uint32_t report_size = 0;
    uint32_t report_count = 0;
    uint8_t report_id = 0;
};

using Bits = std::map<uint8_t, uint32_t>;

std::map<uint8_t, uint16_t> to_bytes(const Bits& bits) {
    std::map<uint8_t, uint16_t> out;
    for (const auto& [id, b] : bits) out[id] = static_cast<uint16_t>((b + 7) / 8 + (id != 0 ? 1 : 0));
    return out;
}

}  // namespace

ReportSizes parse(const uint8_t* desc, size_t len) {
    Bits in, out, feat;
    Globals g;
    std::vector<Globals> stack;

    size_t i = 0;
    while (i < len) {
        const uint8_t prefix = desc[i];
        if (prefix == 0xFE) {  // long item: 0xFE, size, tag, data…
            if (i + 1 >= len) break;
            i += 3 + desc[i + 1];
            continue;
        }
        const size_t size = (prefix & 0x03) == 3 ? 4 : (prefix & 0x03);
        if (i + 1 + size > len) break;
        uint32_t value = 0;
        for (size_t k = 0; k < size; ++k) value |= static_cast<uint32_t>(desc[i + 1 + k]) << (8 * k);
        const uint8_t type = (prefix >> 2) & 0x03;
        const uint8_t tag = prefix >> 4;

        if (type == 1) {  // global
            switch (tag) {
                case 0x7: g.report_size = value; break;
                case 0x8: g.report_id = static_cast<uint8_t>(value); break;
                case 0x9: g.report_count = value; break;
                case 0xA: stack.push_back(g); break;
                case 0xB:
                    if (!stack.empty()) {
                        g = stack.back();
                        stack.pop_back();
                    }
                    break;
                default: break;
            }
        } else if (type == 0) {  // main
            const uint32_t bits = g.report_size * g.report_count;
            switch (tag) {
                case 0x8: in[g.report_id] += bits; break;
                case 0x9: out[g.report_id] += bits; break;
                case 0xB: feat[g.report_id] += bits; break;
                default: break;  // collections
            }
        }
        i += 1 + size;
    }

    return ReportSizes{to_bytes(in), to_bytes(out), to_bytes(feat)};
}

}  // namespace hidparse
