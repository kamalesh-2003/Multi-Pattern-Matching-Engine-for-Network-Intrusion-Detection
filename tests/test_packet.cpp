// Packet framing + reassembly: unit cases plus a robustness sweep that feeds a
// lot of random bytes through parse_stream() to catch out-of-bounds / hangs
// (the same surface fuzz/fuzz_packet.cpp exercises under a real fuzzer).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mpm/gen.hpp"
#include "mpm/packet.hpp"
#include "support/check.hpp"

using namespace mpm;

// Build a valid frame on the wire.
static void put_u16(std::string& s, std::uint16_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
}
static void put_u32(std::string& s, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
static std::string make_frame(std::uint32_t stream, std::uint32_t seq,
                              std::string_view payload) {
    std::string s = "MP";
    s.push_back(1);    // version
    s.push_back(0);    // flags
    put_u32(s, stream);
    put_u32(s, seq);
    put_u16(s, static_cast<std::uint16_t>(payload.size()));
    s.append(payload);
    return s;
}

int main() {
    test::begin("packet");

    // In-order reassembly of one stream.
    {
        std::string wire = make_frame(1, 0, "hello");
        wire += make_frame(1, 5, "world");
        const auto* d = reinterpret_cast<const std::uint8_t*>(wire.data());
        MPM_CHECK_EQ(parse_stream(d, wire.size()), std::string("helloworld"));
    }

    // Out-of-order frames get reordered by seq.
    {
        std::string wire = make_frame(2, 5, "world");
        wire += make_frame(2, 0, "hello");
        const auto* d = reinterpret_cast<const std::uint8_t*>(wire.data());
        MPM_CHECK_EQ(parse_stream(d, wire.size()), std::string("helloworld"));
    }

    // Two interleaved streams.
    {
        std::string wire = make_frame(1, 0, "AA");
        wire += make_frame(2, 0, "bb");
        wire += make_frame(1, 2, "CC");
        wire += make_frame(2, 2, "dd");
        const auto* d = reinterpret_cast<const std::uint8_t*>(wire.data());
        // Output order follows arrival of newly-contiguous bytes.
        MPM_CHECK_EQ(parse_stream(d, wire.size()), std::string("AAbbCCdd"));
    }

    // Retransmit / overlap: duplicate then overlapping frame must not duplicate.
    {
        std::string wire = make_frame(3, 0, "abcd");
        wire += make_frame(3, 0, "abcd");     // full retransmit
        wire += make_frame(3, 2, "cdef");     // overlaps at 2, adds "ef"
        const auto* d = reinterpret_cast<const std::uint8_t*>(wire.data());
        MPM_CHECK_EQ(parse_stream(d, wire.size()), std::string("abcdef"));
    }

    // Truncated header / payload must not crash and yields what completed.
    {
        std::string wire = make_frame(1, 0, "ok");
        wire += "MP";           // dangling partial header
        const auto* d = reinterpret_cast<const std::uint8_t*>(wire.data());
        MPM_CHECK_EQ(parse_stream(d, wire.size()), std::string("ok"));
    }

    // Robustness sweep: random bytes and lightly-corrupted valid streams must
    // never crash / read OOB (ASan in the sanitizer build enforces the latter).
    {
        gen::Rng rng(0xFACEB00CULL);
        for (int it = 0; it < 200000; ++it) {
            std::string buf;
            const int kind = static_cast<int>(rng.below(2));
            if (kind == 0) {
                buf = gen::random_bytes(rng, rng.range(0, 64), 256);
            } else {
                // A valid stream with a few bytes flipped.
                buf = make_frame(rng.below(4), rng.below(32),
                                 gen::random_bytes(rng, rng.range(0, 20), 256));
                const int flips = static_cast<int>(rng.below(4));
                for (int f = 0; f < flips && !buf.empty(); ++f) {
                    buf[rng.below(static_cast<unsigned>(buf.size()))] =
                        static_cast<char>(rng.below(256));
                }
            }
            volatile std::size_t sz =
                parse_stream(reinterpret_cast<const std::uint8_t*>(buf.data()),
                             buf.size())
                    .size();
            (void)sz;
        }
    }

    std::printf("  packet: unit + 200000 robustness cases ok\n");
    return test::finish();
}
