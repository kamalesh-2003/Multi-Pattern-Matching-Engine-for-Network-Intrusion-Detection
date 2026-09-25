// Command-line intrusion-detection driver. Wires the full pipeline together:
//
//   input bytes --[producer thread]--> SPSC ring --[consumer thread]-->
//       StreamScanner (frame parse -> reassemble -> Aho-Corasick scan) -> matches
//
// Usage:
//   nids --patterns FILE [--in FILE] [--block N] [--show]
//   nids --patterns FILE --demo        synthesize a framed stream and scan it
//
// The pattern file has one pattern per line; blank lines and '#' comments are
// skipped; \xHH escapes are supported for binary signatures.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "mpm/aho_corasick.hpp"
#include "mpm/packet.hpp"
#include "mpm/scanner.hpp"
#include "mpm/spsc_ring.hpp"

using namespace mpm;

namespace {

std::string unescape(const std::string& in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            const char n = in[i + 1];
            if (n == 'x' && i + 3 < in.size()) {
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                const int hi = hex(in[i + 2]), lo = hex(in[i + 3]);
                if (hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 3;
                    continue;
                }
            } else if (n == 'n') { out.push_back('\n'); ++i; continue; }
            else if (n == '\\') { out.push_back('\\'); ++i; continue; }
        }
        out.push_back(in[i]);
    }
    return out;
}

std::vector<Pattern> load_patterns(const std::string& path) {
    std::vector<Pattern> pats;
    std::ifstream f(path);
    std::string line;
    std::uint32_t id = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::string bytes = unescape(line);
        if (!bytes.empty()) pats.push_back(Pattern{id++, std::move(bytes)});
    }
    return pats;
}

// Encode one wire frame (mirrors the format parsed by parse_frame).
void put_le(std::string& s, std::uint64_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
std::string make_frame(std::uint32_t stream, std::uint32_t seq, std::string_view payload) {
    std::string s = "MP";
    s.push_back(1);
    s.push_back(0);
    put_le(s, stream, 4);
    put_le(s, seq, 4);
    put_le(s, static_cast<std::uint16_t>(payload.size()), 2);
    s.append(payload);
    return s;
}

// A framed stream that carries the loaded patterns (some out of order) so the
// pipeline has something to find.
std::string synthesize_demo(const std::vector<Pattern>& pats) {
    std::string filler = "the quick brown fox jumps over the lazy dog. ";
    std::string s1;
    for (std::size_t i = 0; i < pats.size(); ++i) {
        s1 += filler;
        s1 += pats[i].bytes;
    }
    std::string wire;
    // Stream 1: split into two frames delivered out of order.
    const std::uint32_t mid = static_cast<std::uint32_t>(s1.size() / 2);
    wire += make_frame(1, mid, std::string_view(s1).substr(mid));       // later half first
    wire += make_frame(1, 0, std::string_view(s1).substr(0, mid));      // earlier half second
    // Stream 2: a single frame.
    wire += make_frame(2, 0, "prefix-" + (pats.empty() ? std::string() : pats[0].bytes));
    return wire;
}

struct Block {
    const char* data;
    std::uint32_t len;
};

} // namespace

int main(int argc, char** argv) {
    std::string patterns_path, in_path;
    std::size_t block = 4096;
    bool demo = false, show = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--patterns" && i + 1 < argc) patterns_path = argv[++i];
        else if (a == "--in" && i + 1 < argc) in_path = argv[++i];
        else if (a == "--block" && i + 1 < argc) block = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--demo") demo = true;
        else if (a == "--show") show = true;
        else { std::fprintf(stderr, "unknown argument: %s\n", a.c_str()); return 2; }
    }
    if (patterns_path.empty()) {
        std::fprintf(stderr, "usage: nids --patterns FILE [--in FILE | --demo] [--block N] [--show]\n");
        return 2;
    }
    if (block == 0) block = 4096;

    std::vector<Pattern> patterns = load_patterns(patterns_path);
    if (patterns.empty()) {
        std::fprintf(stderr, "no patterns loaded from %s\n", patterns_path.c_str());
        return 1;
    }
    AhoCorasick ac(patterns);

    // Acquire the input bytes.
    std::string input;
    if (demo) {
        input = synthesize_demo(patterns);
    } else if (!in_path.empty()) {
        std::ifstream f(in_path, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        input = ss.str();
    } else {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        input = ss.str();
    }

    // Producer thread streams fixed-size blocks through the ring; the consumer
    // reassembles and scans. This is the path the SPSC ring exists for.
    SpscRing<Block> ring(1024);
    std::atomic<bool> done{false};
    std::uint64_t match_count = 0;

    std::thread consumer([&] {
        StreamScanner scanner(ac);
        auto handle = [&](std::uint32_t sid, const Match& m) {
            ++match_count;
            if (show) {
                std::printf("match: stream=%u pattern=%u range=[%llu,%llu)\n", sid,
                            m.id, static_cast<unsigned long long>(m.begin),
                            static_cast<unsigned long long>(m.end));
            }
        };
        Block b{};
        for (;;) {
            if (ring.pop(b)) {
                scanner.feed(std::string_view(b.data, b.len), handle);
            } else if (done.load(std::memory_order_acquire)) {
                while (ring.pop(b)) scanner.feed(std::string_view(b.data, b.len), handle);
                break;
            }
        }
    });

    std::thread producer([&] {
        for (std::size_t off = 0; off < input.size(); off += block) {
            const std::size_t n = std::min(block, input.size() - off);
            Block b{input.data() + off, static_cast<std::uint32_t>(n)};
            while (!ring.push(b)) { /* ring full: spin until the consumer drains */ }
        }
        done.store(true, std::memory_order_release);
    });

    producer.join();
    consumer.join();

    std::printf("patterns=%zu input_bytes=%zu matches=%llu\n", ac.num_patterns(),
                input.size(), static_cast<unsigned long long>(match_count));
    return 0;
}
