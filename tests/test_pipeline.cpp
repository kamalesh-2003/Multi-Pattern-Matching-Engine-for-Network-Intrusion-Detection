// End-to-end pipeline: framed wire bytes -> StreamScanner (parse + reassemble +
// Aho-Corasick) -> matches. Checks that fragmenting the wire at every byte and
// delivering frames out of order does not change the reported matches, and that
// offsets are in reassembled-stream coordinates.
#include <algorithm>
#include <string>
#include <vector>

#include "mpm/aho_corasick.hpp"
#include "mpm/scanner.hpp"
#include "support/check.hpp"

using namespace mpm;

static void put_le(std::string& s, std::uint64_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
static std::string frame(std::uint32_t stream, std::uint32_t seq, std::string_view p) {
    std::string s = "MP";
    s.push_back(1);
    s.push_back(0);
    put_le(s, stream, 4);
    put_le(s, seq, 4);
    put_le(s, static_cast<std::uint16_t>(p.size()), 2);
    s.append(p);
    return s;
}

// Collect matches as "sid:id@begin" for order-independent comparison.
static std::vector<std::string> run(const AhoCorasick& ac, const std::string& wire,
                                    std::size_t chunk) {
    StreamScanner sc(ac);
    std::vector<std::string> hits;
    for (std::size_t p = 0; p < wire.size(); p += chunk) {
        const std::size_t n = std::min(chunk, wire.size() - p);
        sc.feed(std::string_view(wire).substr(p, n), [&](std::uint32_t sid, const Match& m) {
            hits.push_back(std::to_string(sid) + ":" + std::to_string(m.id) + "@" +
                           std::to_string(m.begin));
        });
    }
    std::sort(hits.begin(), hits.end());
    return hits;
}

int main() {
    test::begin("pipeline");

    std::vector<Pattern> pats = {{0, "attack"}, {1, "evil"}, {2, "aa"}};
    AhoCorasick ac(pats);

    // Stream 1 reassembles to "...attack...aa..."; stream 2 to "evil".
    const std::string s1 = "xxattackyyaazz"; // "attack"@2, "aa"@10
    const std::string s2 = "evil";           // "evil"@0
    std::string wire;
    // Deliver stream 1 out of order (second half first), interleave stream 2.
    wire += frame(1, 7, std::string_view(s1).substr(7));
    wire += frame(2, 0, s2);
    wire += frame(1, 0, std::string_view(s1).substr(0, 7));

    // Reference: whole-buffer feed.
    const std::vector<std::string> ref = run(ac, wire, wire.size());
    MPM_CHECK(!ref.empty());

    // Must contain the expected hits.
    bool has_attack = false, has_evil = false, has_aa = false;
    for (const auto& h : ref) {
        if (h == "1:0@2") has_attack = true;
        if (h == "2:1@0") has_evil = true;
        if (h == "1:2@10") has_aa = true;
    }
    MPM_CHECK(has_attack);
    MPM_CHECK(has_evil);
    MPM_CHECK(has_aa);

    // Fragmenting the wire at every chunk size must not change the match set.
    for (std::size_t chunk = 1; chunk <= wire.size(); ++chunk) {
        if (run(ac, wire, chunk) != ref) {
            test::fail(__FILE__, __LINE__,
                       "pipeline differs at chunk size " + std::to_string(chunk));
        }
    }

    return test::finish();
}
