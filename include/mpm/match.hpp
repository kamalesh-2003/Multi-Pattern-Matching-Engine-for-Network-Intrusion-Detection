// Core value types shared by every matcher in the engine.
#ifndef MPM_MATCH_HPP
#define MPM_MATCH_HPP

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mpm {

// A pattern to search for. `bytes` is treated as raw binary (may contain NULs).
// `id` is caller-assigned and echoed back on every match; it need not be unique,
// though the test generators keep pattern strings unique.
struct Pattern {
    std::uint32_t id;
    std::string bytes;
};

// A single occurrence: pattern `id` occupies input bytes [begin, end).
// end - begin == length of the matched pattern.
struct Match {
    std::uint32_t id;
    std::uint64_t begin;
    std::uint64_t end;

    friend bool operator==(const Match& a, const Match& b) {
        return a.id == b.id && a.begin == b.begin && a.end == b.end;
    }
    // Total order used to canonicalise match sets before comparison.
    friend bool operator<(const Match& a, const Match& b) {
        if (a.end != b.end) return a.end < b.end;
        if (a.begin != b.begin) return a.begin < b.begin;
        return a.id < b.id;
    }
};

using Matches = std::vector<Match>;

// Canonicalise a match list (sort) so two lists can be compared as sets/multisets.
inline void canonicalize(Matches& m) {
    std::sort(m.begin(), m.end());
}

} // namespace mpm

#endif // MPM_MATCH_HPP
