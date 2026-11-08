// Minimal HID report descriptor parser: report sizes per report id.
// Pure C++17 so it builds and is tested on the Mac.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

namespace hidparse {

struct ReportSizes {
    // Bytes per report id, including the leading report id byte when the id is non-zero.
    std::map<uint8_t, uint16_t> input;
    std::map<uint8_t, uint16_t> output;
    std::map<uint8_t, uint16_t> feature;
};

ReportSizes parse(const uint8_t* desc, size_t len);

}  // namespace hidparse
