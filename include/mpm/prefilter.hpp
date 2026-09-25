// SIMD byte-set prefilter: find the first buffer position whose byte is in a
// given set, letting a NIDS skip runs that cannot begin any pattern.
//
// The set is stored twice: `table` (flat 256-entry array) for the scalar path,
// and lut_a/lut_b (two 16-byte low-nibble tables) for the pshufb membership
// trick used by the SIMD paths (see src/simd/*.cpp). Runtime CPUID dispatch
// picks AVX2 > SSE4.2 > scalar; each tier is also exported directly so the
// differential test can compare them on identical inputs.
#ifndef MPM_PREFILTER_HPP
#define MPM_PREFILTER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "mpm/match.hpp"

namespace mpm {

struct ByteSet {
    std::array<std::uint8_t, 256> table{};
    std::array<std::uint8_t, 16> lut_a{}; // high nibbles 0..7
    std::array<std::uint8_t, 16> lut_b{}; // high nibbles 8..15

    void clear() {
        table.fill(0);
        lut_a.fill(0);
        lut_b.fill(0);
    }

    void add(std::uint8_t v) {
        if (table[v]) return;
        table[v] = 1;
        const unsigned hn = v >> 4;
        const unsigned ln = v & 0x0F;
        if (hn < 8) {
            lut_a[ln] = static_cast<std::uint8_t>(lut_a[ln] | (1u << hn));
        } else {
            lut_b[ln] = static_cast<std::uint8_t>(lut_b[ln] | (1u << (hn - 8)));
        }
    }

    bool contains(std::uint8_t v) const { return table[v] != 0; }

    // Set of first bytes across all (non-empty) patterns.
    static ByteSet from_first_bytes(const std::vector<Pattern>& patterns) {
        ByteSet s;
        for (const Pattern& p : patterns) {
            if (!p.bytes.empty()) {
                s.add(static_cast<std::uint8_t>(p.bytes.front()));
            }
        }
        return s;
    }
};

enum class SimdTier { Scalar, Sse42, Avx2 };

const char* to_string(SimdTier t);

// Runtime capability queries (backed by __builtin_cpu_supports).
bool cpu_has_sse42();
bool cpu_has_avx2();

// Per-tier entry points. Every buffer is scanned for the first member of `set`;
// the return value is the byte index, or `len` if no byte is in the set.
// The SSE4.2/AVX2 variants are compiled unconditionally but must only be CALLED
// when the corresponding cpu_has_* query is true.
std::size_t prefilter_find_first_scalar(const ByteSet& set, const std::uint8_t* data, std::size_t len);
std::size_t prefilter_find_first_sse42(const ByteSet& set, const std::uint8_t* data, std::size_t len);
std::size_t prefilter_find_first_avx2(const ByteSet& set, const std::uint8_t* data, std::size_t len);

// Dispatched entry point: uses the best tier available on this CPU.
std::size_t prefilter_find_first(const ByteSet& set, const std::uint8_t* data, std::size_t len);
SimdTier prefilter_active_tier();

} // namespace mpm

#endif // MPM_PREFILTER_HPP
