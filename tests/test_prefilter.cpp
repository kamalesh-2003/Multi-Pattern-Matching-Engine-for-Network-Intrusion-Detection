// SIMD prefilter correctness: every compiled tier (SSE4.2, AVX2) must return the
// exact same first-member index as the scalar reference, on >= 1,000,000 random
// (byte-set, buffer) cases. The scalar path is itself checked against an
// independent trivial linear scan.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mpm/gen.hpp"
#include "mpm/prefilter.hpp"
#include "support/check.hpp"

using namespace mpm;

// Independent reference, deliberately not sharing code with the library.
static std::size_t ref_find_first(const ByteSet& set, const std::uint8_t* data,
                                  std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        if (set.table[data[i]]) return i;
    }
    return len;
}

int main(int argc, char** argv) {
    test::begin("prefilter");

    std::printf("  active tier: %s (sse4.2=%d avx2=%d)\n",
                to_string(prefilter_active_tier()), cpu_has_sse42() ? 1 : 0,
                cpu_has_avx2() ? 1 : 0);

    std::uint64_t target = 1'000'000;
    if (argc > 1) target = std::strtoull(argv[1], nullptr, 10);

    gen::Rng rng(0xABCDEF01ULL);

    for (std::uint64_t c = 0; c < target; ++c) {
        // Random set: pick how many members and which bytes.
        ByteSet set;
        const unsigned members = rng.range(0, 40);
        for (unsigned m = 0; m < members; ++m) {
            set.add(static_cast<std::uint8_t>(rng.below(256)));
        }

        // Random buffer: length spans the SIMD tail boundaries (0..80).
        const std::size_t n = rng.range(0, 80);
        std::string buf = gen::random_bytes(rng, n, 256);
        const auto* data = reinterpret_cast<const std::uint8_t*>(buf.data());

        const std::size_t expect = ref_find_first(set, data, n);

        MPM_CHECK_EQ(prefilter_find_first_scalar(set, data, n), expect);
        if (cpu_has_sse42()) {
            MPM_CHECK_EQ(prefilter_find_first_sse42(set, data, n), expect);
        }
        if (cpu_has_avx2()) {
            MPM_CHECK_EQ(prefilter_find_first_avx2(set, data, n), expect);
        }
        MPM_CHECK_EQ(prefilter_find_first(set, data, n), expect);

        if (test::ctx().failures > 0) {
            std::printf("  first failure at case %llu (n=%zu, members=%u)\n",
                        static_cast<unsigned long long>(c), n, members);
            break;
        }
    }

    std::printf("  prefilter: %llu cases\n", static_cast<unsigned long long>(target));
    return test::finish();
}
