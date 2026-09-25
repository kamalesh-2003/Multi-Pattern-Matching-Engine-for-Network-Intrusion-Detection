// SSE4.2 (really SSSE3 pshufb + SSE4.1 blendv) prefilter. Compiled in its own
// translation unit with -msse4.2 so these intrinsics never leak into code that
// might run on an older CPU. Only called when cpu_has_sse42() is true.
//
// Exact arbitrary-256-set membership for 16 bytes at a time:
//   value v -> hn = v>>4, ln = v&0xF.  The set is encoded as two low-nibble
//   lookup tables: lut_a[ln] has bit `hn` set for members with hn in 0..7,
//   lut_b[ln] has bit `hn-8` set for members with hn in 8..15. For each input
//   byte we pshufb the tables by ln, pick lut_a or lut_b by (hn>=8), and test
//   bit (hn&7). A byte is a member iff that bit is set.
#include <immintrin.h>

#include "mpm/prefilter.hpp"

namespace mpm {

std::size_t prefilter_find_first_sse42(const ByteSet& set, const std::uint8_t* data,
                                       std::size_t len) {
    const __m128i luta = _mm_loadu_si128(reinterpret_cast<const __m128i*>(set.lut_a.data()));
    const __m128i lutb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(set.lut_b.data()));
    const __m128i low_mask = _mm_set1_epi8(0x0F);
    const __m128i seven = _mm_set1_epi8(0x07);
    const __m128i eight = _mm_set1_epi8(0x08);
    const __m128i pow2 = _mm_setr_epi8(1, 2, 4, 8, 16, 32, 64, static_cast<char>(0x80),
                                       1, 2, 4, 8, 16, 32, 64, static_cast<char>(0x80));

    std::size_t i = 0;
    for (; i + 16 <= len; i += 16) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
        const __m128i ln = _mm_and_si128(v, low_mask);
        const __m128i hn = _mm_and_si128(_mm_srli_epi16(v, 4), low_mask);
        const __m128i mA = _mm_shuffle_epi8(luta, ln);
        const __m128i mB = _mm_shuffle_epi8(lutb, ln);
        const __m128i selB = _mm_cmpeq_epi8(_mm_and_si128(hn, eight), eight);
        const __m128i chosen = _mm_blendv_epi8(mA, mB, selB);
        const __m128i bit = _mm_shuffle_epi8(pow2, _mm_and_si128(hn, seven));
        const __m128i test = _mm_and_si128(chosen, bit);
        const __m128i member = _mm_cmpeq_epi8(test, bit);
        const unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(member));
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
