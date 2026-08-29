// Primary correctness oracle: the Aho-Corasick engine must agree with the naive
// reference matcher on the full match set for >= 1,000,000 randomized cases.
//
// A "case" is one (pattern-set, input) pair whose match sets are compared. To
// reach a million cases quickly we build an automaton per pattern-set and reuse
// it across many random inputs.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mpm/aho_corasick.hpp"
#include "mpm/gen.hpp"
#include "mpm/naive_matcher.hpp"
#include "support/check.hpp"

using namespace mpm;

int main(int argc, char** argv) {
    test::begin("differential");

    // Default 1,000,000 cases; override via argv[1] for quick local runs.
    std::uint64_t target = 1'000'000;
    if (argc > 1) target = std::strtoull(argv[1], nullptr, 10);

    gen::Rng rng(0xC0FFEEULL);
    std::uint64_t cases = 0;
    std::uint64_t total_matches = 0;

    const unsigned kInputsPerSet = 200;

    while (cases < target) {
        // Randomize the shape of this pattern set.
        const unsigned alphabet = rng.range(2, 6);          // tiny -> dense matches
        const unsigned num_pat = rng.range(1, 12);
        const unsigned min_len = 1;
        const unsigned max_len = rng.range(2, 8);
        std::vector<Pattern> pats =
            gen::random_patterns(rng, num_pat, min_len, max_len, alphabet);
        if (pats.empty()) continue;

        AhoCorasick ac(pats);
        NaiveMatcher nv(pats);

        for (unsigned k = 0; k < kInputsPerSet && cases < target; ++k) {
            const std::size_t n = rng.range(0, 120);
            std::string input = gen::random_bytes(rng, n, alphabet);

            Matches a = ac.scan_all(input);
            Matches b = nv.scan_all(input);
            canonicalize(a);
            canonicalize(b);

            if (a != b) {
                std::string dump;
                for (unsigned char c : input) dump += static_cast<char>('0' + (c % 10));
                test::fail(__FILE__, __LINE__,
                           "mismatch: alphabet=" + std::to_string(alphabet) +
                               " npat=" + std::to_string(pats.size()) +
                               " input=" + dump + " ac=" + std::to_string(a.size()) +
                               " naive=" + std::to_string(b.size()));
            }
            total_matches += a.size();
            ++cases;
        }
    }

    std::printf("  differential: %llu cases, %llu total matches\n",
                static_cast<unsigned long long>(cases),
                static_cast<unsigned long long>(total_matches));
    return test::finish();
}
