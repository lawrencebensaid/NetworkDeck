#include <doctest/doctest.h>

#include "cora/frame.hpp"

using namespace cora;

static Frame sample(uint32_t id, std::vector<uint8_t> payload, uint16_t fl = flags::kVerbatim) {
    Frame f;
    f.flags = fl;
    f.op = HidOp::GetReport;
    f.message_id = id;
    f.payload = std::move(payload);
    return f;
}

TEST_CASE("encode writes the 16-byte header little-endian") {
    auto b = encode(sample(0x01020304, {0xAA, 0xBB}));
    REQUIRE(b.size() == 18);
    CHECK(b[0] == 0x43);
    CHECK(b[1] == 0x93);
    CHECK(b[2] == 0x8A);
    CHECK(b[3] == 0x41);
    CHECK(b[4] == 0x00);  // flags 0x8000 LE
    CHECK(b[5] == 0x80);
    CHECK(b[6] == 0x02);  // GET_REPORT
    CHECK(b[7] == 0x00);
    CHECK(b[8] == 0x04);
    CHECK(b[11] == 0x01);
    CHECK(b[12] == 2);
    CHECK(b[16] == 0xAA);
    CHECK(b[17] == 0xBB);
}

TEST_CASE("decoder round-trips frames split across and merged within TCP segments") {
    auto a = encode(sample(1, std::vector<uint8_t>(1024, 0x11)));
    auto b = encode(sample(2, {0x22}));
    std::vector<uint8_t> stream(a);
    stream.insert(stream.end(), b.begin(), b.end());

    Decoder d;
    std::vector<Frame> got;
    for (size_t i = 0; i < stream.size(); i += 7) {  // odd chunking
        d.feed(stream.data() + i, std::min<size_t>(7, stream.size() - i));
        while (auto f = d.next()) got.push_back(*f);
    }
    REQUIRE(got.size() == 2);
    CHECK(got[0].message_id == 1);
    CHECK(got[0].payload.size() == 1024);
    CHECK(got[0].op == HidOp::GetReport);
    CHECK(got[0].flags == flags::kVerbatim);
    CHECK(got[1].message_id == 2);
    CHECK(got[1].payload == std::vector<uint8_t>{0x22});
    CHECK(d.buffered() == 0);
}

TEST_CASE("decoder skips garbage before the magic") {
    std::vector<uint8_t> stream = {0x00, 0x43, 0x93, 0x01};
    auto f = encode(sample(7, {0x01}));
    stream.insert(stream.end(), f.begin(), f.end());
    Decoder d;
    d.feed(stream.data(), stream.size());
    auto got = d.next();
    REQUIRE(got);
    CHECK(got->message_id == 7);
    CHECK(d.discarded() == 4);
}

TEST_CASE("decoder keeps a magic split across feeds") {
    auto f = encode(sample(9, {0x05}));
    Decoder d;
    std::vector<uint8_t> junk = {0xFF, 0xFF, 0x43, 0x93};
    d.feed(junk.data(), junk.size());
    CHECK_FALSE(d.next());
    d.feed(f.data() + 2, f.size() - 2);
    auto got = d.next();
    REQUIRE(got);
    CHECK(got->message_id == 9);
}

TEST_CASE("decoder rejects an absurd length and resyncs on the next magic") {
    std::vector<uint8_t> bogus = {0x43, 0x93, 0x8A, 0x41, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x7F};
    auto f = encode(sample(3, {0x09}));
    bogus.insert(bogus.end(), f.begin(), f.end());
    Decoder d;
    d.feed(bogus.data(), bogus.size());
    auto got = d.next();
    REQUIRE(got);
    CHECK(got->message_id == 3);
}
