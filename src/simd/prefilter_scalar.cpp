// Scalar prefilter + runtime dispatch. Compiled with the baseline -march; must
// NOT contain any SSE/AVX intrinsics so it is always safe to run.
#include "mpm/prefilter.hpp"

namespace mpm {

const char* to_string(SimdTier t) {
    switch (t) {
        case SimdTier::Scalar: return "scalar";
        case SimdTier::Sse42:  return "sse4.2";
        case SimdTier::Avx2:   return "avx2";
    }
    return "unknown";
}

bool cpu_has_sse42() {
    __builtin_cpu_init();
    return __builtin_cpu_supports("sse4.2") != 0;
}

bool cpu_has_avx2() {
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") != 0;
}

std::size_t prefilter_find_first_scalar(const ByteSet& set, const std::uint8_t* data,
                                        std::size_t len) {
    const std::uint8_t* tbl = set.table.data();
    for (std::size_t i = 0; i < len; ++i) {
        if (tbl[data[i]]) return i;
    }
    return len;
}

namespace {
using PrefilterFn = std::size_t (*)(const ByteSet&, const std::uint8_t*, std::size_t);

SimdTier choose_tier() {
    if (cpu_has_avx2()) return SimdTier::Avx2;
    if (cpu_has_sse42()) return SimdTier::Sse42;
    return SimdTier::Scalar;
}

PrefilterFn resolve() {
    switch (choose_tier()) {
        case SimdTier::Avx2:  return &prefilter_find_first_avx2;
        case SimdTier::Sse42: return &prefilter_find_first_sse42;
        case SimdTier::Scalar: break;
    }
    return &prefilter_find_first_scalar;
}

// Resolved once at load time; the branch never appears in the hot path.
const SimdTier g_tier = choose_tier();
const PrefilterFn g_fn = resolve();
} // namespace

SimdTier prefilter_active_tier() { return g_tier; }

std::size_t prefilter_find_first(const ByteSet& set, const std::uint8_t* data,
                                 std::size_t len) {
    return g_fn(set, data, len);
}

} // namespace mpm
