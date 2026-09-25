// Streaming equivalence: scanning an input as one block must produce exactly the
// same match set as scanning it split at arbitrary chunk boundaries -- including
// the worst case of one byte at a time. This guards the streaming State threaded
// across scan() calls.
#include <cstdio>
#include <string>
#include <vector>

#include "mpm/aho_corasick.hpp"
#include "mpm/gen.hpp"
#include "support/check.hpp"

using namespace mpm;

// Scan `input` feeding it in chunks of the given boundary sizes.
static Matches scan_chunked(const AhoCorasick& ac, const std::string& input,
                            const std::vector<std::size_t>& boundaries) {
    Matches out;
    AhoCorasick::State st;
    std::size_t pos = 0;
    for (std::size_t b : boundaries) {
        std::size_t take = std::min(b, input.size() - pos);
        ac.scan(std::string_view(input).substr(pos, take), st,
                [&](const Match& m) { out.push_back(m); });
        pos += take;
        if (pos >= input.size()) break;
    }
    // Any remainder in one shot.
    if (pos < input.size()) {
        ac.scan(std::string_view(input).substr(pos), st,
                [&](const Match& m) { out.push_back(m); });
    }
    canonicalize(out);
    return out;
}

int main(int argc, char** argv) {
    test::begin("streaming");

    std::uint64_t iters = 20000;
    if (argc > 1) iters = std::strtoull(argv[1], nullptr, 10);

    gen::Rng rng(0x5EED1234ULL);

    for (std::uint64_t it = 0; it < iters; ++it) {
        const unsigned alphabet = rng.range(2, 5);
        std::vector<Pattern> pats =
            gen::random_patterns(rng, rng.range(1, 8), 1, rng.range(2, 7), alphabet);
        if (pats.empty()) continue;
        AhoCorasick ac(pats);

        const std::size_t n = rng.range(0, 80);
        std::string input = gen::random_bytes(rng, n, alphabet);

        // Reference: one block.
        Matches whole = ac.scan_all(input);
        canonicalize(whole);

        // 1) One byte at a time.
        {
            std::vector<std::size_t> ones(input.size() ? input.size() : 1, 1);
            Matches split = scan_chunked(ac, input, ones);
            if (split != whole) {
                test::fail(__FILE__, __LINE__, "byte-split differs at iter " +
                                                   std::to_string(it));
            }
        }

        // 2) A handful of random boundary splits.
        for (int trial = 0; trial < 4; ++trial) {
            std::vector<std::size_t> bounds;
            std::size_t remaining = input.size();
            while (remaining > 0) {
                std::size_t chunk = rng.range(1, static_cast<unsigned>(remaining));
                bounds.push_back(chunk);
                remaining -= chunk;
            }
            if (bounds.empty()) bounds.push_back(0);
            Matches split = scan_chunked(ac, input, bounds);
            if (split != whole) {
                test::fail(__FILE__, __LINE__, "random-split differs at iter " +
                                                   std::to_string(it));
            }
        }
    }

    std::printf("  streaming: %llu iterations ok\n",
                static_cast<unsigned long long>(iters));
    return test::finish();
}
