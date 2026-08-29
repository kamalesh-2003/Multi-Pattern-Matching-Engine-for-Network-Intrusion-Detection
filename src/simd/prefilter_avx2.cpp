// AVX2 prefilter: 32 bytes per iteration. Same exact-membership algorithm as
// the SSE4.2 path (see prefilter_sse42.cpp). Own translation unit, compiled
// with -mavx2; only called when cpu_has_avx2() is true.
#include <immintrin.h>

#include "mpm/prefilter.hpp"

namespace mpm {

std::size_t prefilter_find_first_avx2(const ByteSet& set, const std::uint8_t* data,
                                      std::size_t len) {
    const __m128i luta128 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(set.lut_a.data()));
    const __m128i lutb128 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(set.lut_b.data()));
    const __m128i pow2_128 = _mm_setr_epi8(1, 2, 4, 8, 16, 32, 64, static_cast<char>(0x80),
                                           1, 2, 4, 8, 16, 32, 64, static_cast<char>(0x80));
    const __m256i luta = _mm256_broadcastsi128_si256(luta128);
    const __m256i lutb = _mm256_broadcastsi128_si256(lutb128);
    const __m256i pow2 = _mm256_broadcastsi128_si256(pow2_128);
    const __m256i low_mask = _mm256_set1_epi8(0x0F);
    const __m256i seven = _mm256_set1_epi8(0x07);
    const __m256i eight = _mm256_set1_epi8(0x08);

    std::size_t i = 0;
    for (; i + 32 <= len; i += 32) {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
        const __m256i ln = _mm256_and_si256(v, low_mask);
        const __m256i hn = _mm256_and_si256(_mm256_srli_epi16(v, 4), low_mask);
        const __m256i mA = _mm256_shuffle_epi8(luta, ln);
        const __m256i mB = _mm256_shuffle_epi8(lutb, ln);
        const __m256i selB = _mm256_cmpeq_epi8(_mm256_and_si256(hn, eight), eight);
        const __m256i chosen = _mm256_blendv_epi8(mA, mB, selB);
        const __m256i bit = _mm256_shuffle_epi8(pow2, _mm256_and_si256(hn, seven));
        const __m256i test = _mm256_and_si256(chosen, bit);
        const __m256i member = _mm256_cmpeq_epi8(test, bit);
        const unsigned mask = static_cast<unsigned>(_mm256_movemask_epi8(member));
        if (mask != 0) {
            return i + static_cast<std::size_t>(__builtin_ctz(mask));
        }
    }
    const std::uint8_t* tbl = set.table.data();
    for (; i < len; ++i) {
        if (tbl[data[i]]) return i;
    }
    return len;
}

} // namespace mpm
