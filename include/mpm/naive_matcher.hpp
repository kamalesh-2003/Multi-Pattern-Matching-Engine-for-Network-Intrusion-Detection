// Naive reference matcher: the correctness oracle.
//
// Deliberately the simplest thing that is obviously correct: for every pattern,
// slide it across the input and compare byte by byte. O(n * sum(len)). It is
// never used in the hot path -- only to validate the real automaton via
// differential testing.
#ifndef MPM_NAIVE_MATCHER_HPP
#define MPM_NAIVE_MATCHER_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "mpm/match.hpp"

namespace mpm {

class NaiveMatcher {
public:
    explicit NaiveMatcher(std::vector<Pattern> patterns)
        : patterns_(std::move(patterns)) {}

    // Report every occurrence of every (non-empty) pattern, including
    // overlapping ones. Matches are emitted in no particular order; the caller
    // canonicalises before comparing.
    template <typename Fn>
    void scan(std::string_view input, Fn&& emit) const {
        const char* data = input.data();
        const std::size_t n = input.size();
        for (const Pattern& p : patterns_) {
            const std::size_t m = p.bytes.size();
            if (m == 0 || m > n) continue;
            const char* pat = p.bytes.data();
            const std::size_t last = n - m;
            for (std::size_t s = 0; s <= last; ++s) {
                if (std::memcmp(data + s, pat, m) == 0) {
                    emit(Match{p.id, static_cast<std::uint64_t>(s),
                               static_cast<std::uint64_t>(s + m)});
                }
            }
        }
    }

    Matches scan_all(std::string_view input) const {
        Matches out;
        scan(input, [&](const Match& mt) { out.push_back(mt); });
        return out;
    }

private:
    std::vector<Pattern> patterns_;
};

} // namespace mpm

#endif // MPM_NAIVE_MATCHER_HPP
