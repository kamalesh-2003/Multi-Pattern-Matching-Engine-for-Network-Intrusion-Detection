// Deterministic workload generators shared by the tests and the benchmark.
// Seeded explicitly so every run is reproducible.
#ifndef MPM_GEN_HPP
#define MPM_GEN_HPP

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "mpm/match.hpp"

namespace mpm::gen {

// Small, fast, deterministic PRNG (splitmix64). Not for cryptography.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Uniform in [0, n).
    std::uint32_t below(std::uint32_t n) {
        return static_cast<std::uint32_t>(next() % n);
    }

    std::uint32_t range(std::uint32_t lo, std::uint32_t hi) { // inclusive
        return lo + below(hi - lo + 1);
    }

private:
    std::uint64_t state_;
};

// A random byte string of length `len` drawn from an alphabet of `alphabet`
// distinct byte values {0, 1, ..., alphabet-1}. A small alphabet makes matches
// (and near-matches) frequent, which stresses the automaton harder.
inline std::string random_bytes(Rng& rng, std::size_t len, unsigned alphabet) {
    std::string s;
    s.resize(len);
    for (std::size_t i = 0; i < len; ++i) {
        s[i] = static_cast<char>(rng.below(alphabet));
    }
    return s;
}

// `count` patterns of length in [min_len, max_len], unique strings, ids 0..n-1.
inline std::vector<Pattern> random_patterns(Rng& rng, unsigned count, unsigned min_len,
                                            unsigned max_len, unsigned alphabet) {
    std::vector<Pattern> pats;
    std::unordered_set<std::string> seen;
    std::uint32_t id = 0;
    unsigned attempts = 0;
    const unsigned cap = count * 64 + 1024;
    while (pats.size() < count && attempts++ < cap) {
        std::string b = random_bytes(rng, rng.range(min_len, max_len), alphabet);
        if (seen.insert(b).second) {
            pats.push_back(Pattern{id++, std::move(b)});
        }
    }
    return pats;
}

} // namespace mpm::gen

#endif // MPM_GEN_HPP
