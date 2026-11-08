#include <doctest/doctest.h>

#include "hidparse/hid_descriptor.hpp"

TEST_CASE("sizes per report id include the id byte") {
    // Vendor page, 1-byte fields: input id 1 x 511, output id 2 x 1023, feature ids 3 and 6 x 31.
    const uint8_t desc[] = {
        0x06, 0x0C, 0xFF,        // Usage Page (vendor)
        0x09, 0x01,              // Usage
        0xA1, 0x01,              // Collection (Application)
        0x75, 0x08,              //   Report Size 8
        0x85, 0x01,              //   Report ID 1
        0x96, 0xFF, 0x01,        //   Report Count 511
        0x09, 0x01, 0x81, 0x02,  //   Usage, Input
        0x85, 0x02,              //   Report ID 2
        0x96, 0xFF, 0x03,        //   Report Count 1023
        0x09, 0x01, 0x91, 0x02,  //   Usage, Output
        0x85, 0x03,              //   Report ID 3
        0x95, 0x1F,              //   Report Count 31
        0x09, 0x01, 0xB1, 0x04,  //   Usage, Feature
        0x85, 0x06,              //   Report ID 6
        0x09, 0x01, 0xB1, 0x04,  //   Usage, Feature (count still 31)
        0xC0,                    // End Collection
    };
    auto s = hidparse::parse(desc, sizeof(desc));
    CHECK(s.input.at(1) == 512);
    CHECK(s.output.at(2) == 1024);
    CHECK(s.feature.at(3) == 32);
    CHECK(s.feature.at(6) == 32);
}

TEST_CASE("no report id means no id byte") {
    const uint8_t desc[] = {0x75, 0x08, 0x95, 0x10, 0x81, 0x02};
    auto s = hidparse::parse(desc, sizeof(desc));
    CHECK(s.input.at(0) == 16);
}

TEST_CASE("push and pop restore globals") {
    const uint8_t desc[] = {0x75, 0x08, 0x95, 0x04, 0xA4, 0x95, 0x10, 0xB4, 0x81, 0x02};
    auto s = hidparse::parse(desc, sizeof(desc));
    CHECK(s.input.at(0) == 4);
}

TEST_CASE("truncated descriptors don't read past the end") {
    const uint8_t desc[] = {0x75, 0x08, 0x96, 0xFF};
    auto s = hidparse::parse(desc, sizeof(desc));
    CHECK(s.input.empty());
}
