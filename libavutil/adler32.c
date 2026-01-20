/*
 * Compute the Adler-32 checksum of a data stream.
 * This is a modified version based on adler32.c from the zlib library.
 *
 * Copyright (C) 1995 Mark Adler
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/**
 * @file
 * Computes the Adler-32 checksum of a data stream
 *
 * This is a modified version based on adler32.c from the zlib library.
 * @author Mark Adler
 * @ingroup lavu_adler32
 */

#include "config.h"
#include "adler32.h"
#include "intreadwrite.h"
#include "macros.h"

#if defined(__x86_64__) || defined(__aarch64__)

#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#define NMAX 5552
#define BASE 65521U

static inline uint32_t adler32_len_1(uint32_t adler, const uint8_t *buf, uint32_t sum2) {
    uint8_t c = *buf;
    adler += c;
    adler %= BASE;
    sum2 += adler;
    sum2 %= BASE;
    return adler | (sum2 << 16);
}

static inline uint32_t adler32_len_16(uint32_t adler, const uint8_t *buf, size_t len, uint32_t sum2) {
    while (len--) {
        uint8_t c = *buf++;
        adler += c;
        sum2 += adler;
    }
    adler %= BASE;
    sum2 %= BASE;
    return adler | (sum2 << 16);
}

#if defined(__x86_64__)

#include <immintrin.h>

static inline uint32_t partial_hsum128(__m128i x) {
    __m128i second_int = _mm_srli_si128(x, 8);
    __m128i sum = _mm_add_epi32(x, second_int);
    return _mm_cvtsi128_si32(sum);
}

static inline uint32_t hsum128(__m128i x) {
    __m128i sum1 = _mm_unpackhi_epi64(x, x);
    __m128i sum2 = _mm_add_epi32(x, sum1);
    __m128i sum3 = _mm_shuffle_epi32(sum2, 0x01);
    __m128i sum4 = _mm_add_epi32(sum2, sum3);
    return _mm_cvtsi128_si32(sum4);
}

static inline uint32_t partial_hsum256(__m256i x) {
    const __m256i perm_vec = _mm256_setr_epi32(0, 2, 4, 6, 1, 1, 1, 1);
    __m256i non_zero = _mm256_permutevar8x32_epi32(x, perm_vec);
    __m128i non_zero_sse = _mm256_castsi256_si128(non_zero);
    __m128i sum2  = _mm_add_epi32(non_zero_sse,_mm_unpackhi_epi64(non_zero_sse, non_zero_sse));
    __m128i sum3  = _mm_add_epi32(sum2, _mm_shuffle_epi32(sum2, 1));
    return (uint32_t)_mm_cvtsi128_si32(sum3);
}

static inline uint32_t hsum256(__m256i x) {
    __m128i sum1  = _mm_add_epi32(_mm256_extracti128_si256(x, 1),
                                  _mm256_castsi256_si128(x));
    __m128i sum2  = _mm_add_epi32(sum1, _mm_unpackhi_epi64(sum1, sum1));
    __m128i sum3  = _mm_add_epi32(sum2, _mm_shuffle_epi32(sum2, 1));
    return (uint32_t)_mm_cvtsi128_si32(sum3);
}

static inline uint32_t partial_hsum512(__m512i x) {
    const __m512i perm_vec = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14,
                                               1, 1, 1, 1, 1,  1,  1,  1);
    __m512i non_zero = _mm512_permutexvar_epi32(perm_vec, x);
    __m256i non_zero_avx = _mm512_castsi512_si256(non_zero);
    __m128i sum1  = _mm_add_epi32(_mm256_extracti128_si256(non_zero_avx, 1),
                                  _mm256_castsi256_si128(non_zero_avx));
    __m128i sum2  = _mm_add_epi32(sum1,_mm_unpackhi_epi64(sum1, sum1));
    __m128i sum3  = _mm_add_epi32(sum2,_mm_shuffle_epi32(sum2, 1));
    return (uint32_t)_mm_cvtsi128_si32(sum3);
}

static inline uint32_t _mm512_reduce_add_epu32(__m512i x) {
    __m256i a = _mm512_extracti64x4_epi64(x, 1);
    __m256i b = _mm512_extracti64x4_epi64(x, 0);
    __m256i a_plus_b = _mm256_add_epi32(a, b);
    __m128i c = _mm256_extracti128_si256(a_plus_b, 1);
    __m128i d = _mm256_extracti128_si256(a_plus_b, 0);
    __m128i c_plus_d = _mm_add_epi32(c, d);
    __m128i sum1 = _mm_unpackhi_epi64(c_plus_d, c_plus_d);
    __m128i sum2 = _mm_add_epi32(sum1, c_plus_d);
    __m128i sum3 = _mm_shuffle_epi32(sum2, 0x01);
    __m128i sum4 = _mm_add_epi32(sum2, sum3);
    return _mm_cvtsi128_si32(sum4);
}

static inline uint32_t adler32_ssse3(uint32_t adler, const uint8_t *buf, size_t len) {
    uint32_t sum2;
    sum2 = (adler >> 16) & 0xffff;
    adler &= 0xffff;
    if (UNLIKELY(len == 1))
        return adler32_len_1(adler, buf, sum2);
    if (UNLIKELY(len < 16))
        return adler32_len_16(adler, buf, len, sum2);
    const __m128i dot2v = _mm_setr_epi8(32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17);
    const __m128i dot2v_0 = _mm_setr_epi8(16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1);
    const __m128i dot3v = _mm_set1_epi16(1);
    const __m128i zero = _mm_setzero_si128();
    __m128i vbuf, vs1_0, vs3, vs1, vs2, vs2_0, v_sad_sum1, v_short_sum2, v_short_sum2_0,
            vbuf_0, v_sad_sum2, vsum2, vsum2_0;
    size_t max_iters = NMAX;
    size_t rem = (uintptr_t)buf & 15;
    size_t align_offset = 16 - rem;
    size_t k = 0;
    if (rem) {
        if (len < 16 + align_offset) {
            vbuf = _mm_loadu_si128((__m128i*)buf);
            len -= 16;
            buf += 16;
            vs1 = _mm_cvtsi32_si128(adler);
            vs2 = _mm_cvtsi32_si128(sum2);
            vs3 = _mm_setzero_si128();
            vs1_0 = vs1;
            goto unaligned_jmp;
        }
        for (size_t i = 0; i < align_offset; ++i) {
            adler += *(buf++);
            sum2 += adler;
        }
        len -= align_offset;
        max_iters -= align_offset;
    }
    while (len >= 16) {
        vs1 = _mm_cvtsi32_si128(adler);
        vs2 = _mm_cvtsi32_si128(sum2);
        vs3 = _mm_setzero_si128();
        vs2_0 = _mm_setzero_si128();
        vs1_0 = vs1;
        k = (len < max_iters ? len : max_iters);
        k -= k % 16;
        len -= k;
        while (k >= 32) {
            vbuf = _mm_load_si128((__m128i*)buf);
            vbuf_0 = _mm_load_si128((__m128i*)(buf + 16));
            buf += 32;
            k -= 32;
            v_sad_sum1 = _mm_sad_epu8(vbuf, zero);
            v_sad_sum2 = _mm_sad_epu8(vbuf_0, zero);
            vs1 = _mm_add_epi32(v_sad_sum1, vs1);
            vs3 = _mm_add_epi32(vs1_0, vs3);
            vs1 = _mm_add_epi32(v_sad_sum2, vs1);
            v_short_sum2 = _mm_maddubs_epi16(vbuf, dot2v);
            vsum2 = _mm_madd_epi16(v_short_sum2, dot3v);
            v_short_sum2_0 = _mm_maddubs_epi16(vbuf_0, dot2v_0);
            vs2 = _mm_add_epi32(vsum2, vs2);
            vsum2_0 = _mm_madd_epi16(v_short_sum2_0, dot3v);
            vs2_0 = _mm_add_epi32(vsum2_0, vs2_0);
            vs1_0 = vs1;
        }
        vs2 = _mm_add_epi32(vs2_0, vs2);
        vs3 = _mm_slli_epi32(vs3, 5);
        vs2 = _mm_add_epi32(vs3, vs2);
        vs3 = _mm_setzero_si128();
        while (k >= 16) {
            vbuf = _mm_load_si128((__m128i*)buf);
            buf += 16;
            k -= 16;
unaligned_jmp:
            v_sad_sum1 = _mm_sad_epu8(vbuf, zero);
            vs1 = _mm_add_epi32(v_sad_sum1, vs1);
            vs3 = _mm_add_epi32(vs1_0, vs3);
            v_short_sum2 = _mm_maddubs_epi16(vbuf, dot2v_0);
            vsum2 = _mm_madd_epi16(v_short_sum2, dot3v);
            vs2 = _mm_add_epi32(vsum2, vs2);
            vs1_0 = vs1;
        }
        vs3 = _mm_slli_epi32(vs3, 4);
        vs2 = _mm_add_epi32(vs2, vs3);

        adler = partial_hsum128(vs1) % BASE;
        sum2 = hsum128(vs2) % BASE;
        max_iters = NMAX;
    }
    return adler32_len_16(adler, buf, len, sum2);
}

static inline uint32_t adler32_avx2(uint32_t adler, const uint8_t *src, size_t len) {
    uint32_t adler0, adler1;
    adler1 = (adler >> 16) & 0xffff;
    adler0 = adler & 0xffff;
rem_peel:
    if (len < 16) {
        return adler32_len_16(adler0, src, len, adler1);
    } else if (len < 32) {
        return adler32_ssse3(adler, src, len);
    }
    __m256i vs1, vs2, vs2_0;
    const __m256i dot2v = _mm256_setr_epi8(64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47,
                                           46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33);
    const __m256i dot2v_0 = _mm256_setr_epi8(32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15,
                                             14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1);
    const __m256i dot3v = _mm256_set1_epi16(1);
    const __m256i zero = _mm256_setzero_si256();
    while (len >= 32) {
        vs1 = _mm256_zextsi128_si256(_mm_cvtsi32_si128(adler0));
        vs2 = _mm256_zextsi128_si256(_mm_cvtsi32_si128(adler1));
        __m256i vs1_0 = vs1;
        __m256i vs3 = _mm256_setzero_si256();
        vs2_0 = vs3;
        size_t k = MIN(len, NMAX);
        k -= k % 32;
        len -= k;
        while (k >= 64) {
            __m256i vbuf = _mm256_loadu_si256((__m256i*)src);
            __m256i vbuf_0 = _mm256_loadu_si256((__m256i*)(src + 32));
            src += 64;
            k -= 64;
            __m256i vs1_sad = _mm256_sad_epu8(vbuf, zero);
            __m256i vs1_sad2 = _mm256_sad_epu8(vbuf_0, zero);
            vs1 = _mm256_add_epi32(vs1, vs1_sad);
            vs3 = _mm256_add_epi32(vs3, vs1_0);
            __m256i v_short_sum2 = _mm256_maddubs_epi16(vbuf, dot2v); // sum 32 uint8s to 16 shorts
            __m256i v_short_sum2_0 = _mm256_maddubs_epi16(vbuf_0, dot2v_0); // sum 32 uint8s to 16 shorts
            __m256i vsum2 = _mm256_madd_epi16(v_short_sum2, dot3v); // sum 16 shorts to 8 uint32s
            __m256i vsum2_0 = _mm256_madd_epi16(v_short_sum2_0, dot3v); // sum 16 shorts to 8 uint32s
            vs1 = _mm256_add_epi32(vs1_sad2, vs1);
            vs2 = _mm256_add_epi32(vsum2, vs2);
            vs2_0 = _mm256_add_epi32(vsum2_0, vs2_0);
            vs1_0 = vs1;
        }
        vs2 = _mm256_add_epi32(vs2_0, vs2);
        vs3 = _mm256_slli_epi32(vs3, 6);
        vs2 = _mm256_add_epi32(vs3, vs2);
        vs3 = _mm256_setzero_si256();
        while (k >= 32) {
            __m256i vbuf = _mm256_loadu_si256((__m256i*)src);
            src += 32;
            k -= 32;
            __m256i vs1_sad = _mm256_sad_epu8(vbuf, zero); // Sum of abs diff, resulting in 2 x int32's
            vs1 = _mm256_add_epi32(vs1, vs1_sad);
            vs3 = _mm256_add_epi32(vs3, vs1_0);
            __m256i v_short_sum2 = _mm256_maddubs_epi16(vbuf, dot2v_0); // sum 32 uint8s to 16 shorts
            __m256i vsum2 = _mm256_madd_epi16(v_short_sum2, dot3v); // sum 16 shorts to 8 uint32s
            vs2 = _mm256_add_epi32(vsum2, vs2);
            vs1_0 = vs1;
        }
        vs3 = _mm256_slli_epi32(vs3, 5);
        vs2 = _mm256_add_epi32(vs2, vs3);
        adler0 = partial_hsum256(vs1) % BASE;
        adler1 = hsum256(vs2) % BASE;
    }
    adler = adler0 | (adler1 << 16);
    if (len) {
        goto rem_peel;
    }
    return adler;
}

static inline uint32_t adler32_avx512_vnni(uint32_t adler, const uint8_t *src, size_t len) {
    uint32_t adler0, adler1;
    adler1 = (adler >> 16) & 0xffff;
    adler0 = adler & 0xffff;
rem_peel:
    if (len < 32)
        return adler32_ssse3(adler, src, len);
    if (len < 64)
        return adler32_avx2(adler, src, len);
    const __m512i dot2v = _mm512_set_epi8(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                          20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
                                          38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
                                          56, 57, 58, 59, 60, 61, 62, 63, 64);
    const __m512i zero = _mm512_setzero_si512();
    __m512i vs1, vs2;
    while (len >= 64) {
        vs1 = _mm512_zextsi128_si512(_mm_cvtsi32_si128(adler0));
        vs2 = _mm512_zextsi128_si512(_mm_cvtsi32_si128(adler1));
        size_t k = MIN(len, NMAX);
        k -= k % 64;
        len -= k;
        __m512i vs1_0 = vs1;
        __m512i vs3 = _mm512_setzero_si512();
        __m512i vs2_1 = _mm512_setzero_si512();
        __m512i vbuf0, vbuf1;
        if (k % 128) {
            vbuf1 = _mm512_loadu_si512((__m512i*)src);
            src += 64;
            k -= 64;
            __m512i vs1_sad = _mm512_sad_epu8(vbuf1, zero);
            vs1 = _mm512_add_epi32(vs1, vs1_sad);
            vs3 = _mm512_add_epi32(vs3, vs1_0);
            vs2 = _mm512_dpbusd_epi32(vs2, vbuf1, dot2v);
            vs1_0 = vs1;
        }
        while (k >= 128) {
            vbuf0 = _mm512_loadu_si512((__m512i*)src);
            vbuf1 = _mm512_loadu_si512((__m512i*)(src + 64));
            src += 128;
            k -= 128;
            __m512i vs1_sad = _mm512_sad_epu8(vbuf0, zero);
            vs1 = _mm512_add_epi32(vs1, vs1_sad);
            vs3 = _mm512_add_epi32(vs3, vs1_0);
            vs2 = _mm512_dpbusd_epi32(vs2, vbuf0, dot2v);
            vs3 = _mm512_add_epi32(vs3, vs1);
            vs1_sad = _mm512_sad_epu8(vbuf1, zero);
            vs1 = _mm512_add_epi32(vs1, vs1_sad);
            vs2_1 = _mm512_dpbusd_epi32(vs2_1, vbuf1, dot2v);
            vs1_0 = vs1;
        }
        vs3 = _mm512_slli_epi32(vs3, 6);
        vs2 = _mm512_add_epi32(vs2, vs3);
        vs2 = _mm512_add_epi32(vs2, vs2_1);
        adler0 = partial_hsum512(vs1) % BASE;
        adler1 = _mm512_reduce_add_epu32(vs2) % BASE;
    }
    adler = adler0 | (adler1 << 16);
    if (len) {
        goto rem_peel;
    }
    return adler;
}

AVAdler av_adler32_update(AVAdler adler, const uint8_t *buf, size_t len){
    #if defined(__AVX512VNNI__)
        return adler32_avx512_vnni(adler, buf, len);
    #elif defined(__AVX2__)
        return adler32_avx2(adler, buf, len);
    #elif defined(__SSSE3__)
        return adler32_ssse3(adler, buf, len);
    #endif
}

#elif defined(__aarch64__)

#include <arm_neon.h>

#define ALIGNED_(x) __attribute__ ((aligned(x)))

static const uint16_t ALIGNED_(64) taps[64] = {
    64, 63, 62, 61, 60, 59, 58, 57,
    56, 55, 54, 53, 52, 51, 50, 49,
    48, 47, 46, 45, 44, 43, 42, 41,
    40, 39, 38, 37, 36, 35, 34, 33,
    32, 31, 30, 29, 28, 27, 26, 25,
    24, 23, 22, 21, 20, 19, 18, 17,
    16, 15, 14, 13, 12, 11, 10, 9,
    8, 7, 6, 5, 4, 3, 2, 1 };

static inline void NEON_accum32_copy(uint32_t *s, uint8_t *dst, const uint8_t *buf, size_t len) {
    uint32x4_t adacc = vdupq_n_u32(0);
    uint32x4_t s2acc = vdupq_n_u32(0);
    uint32x4_t s2acc_0 = vdupq_n_u32(0);
    uint32x4_t s2acc_1 = vdupq_n_u32(0);
    uint32x4_t s2acc_2 = vdupq_n_u32(0);
    adacc = vsetq_lane_u32(s[0], adacc, 0);
    s2acc = vsetq_lane_u32(s[1], s2acc, 0);
    uint32x4_t s3acc = vdupq_n_u32(0);
    uint32x4_t adacc_prev = adacc;
    uint16x8_t s2_0, s2_1, s2_2, s2_3;
    s2_0 = s2_1 = s2_2 = s2_3 = vdupq_n_u16(0);
    uint16x8_t s2_4, s2_5, s2_6, s2_7;
    s2_4 = s2_5 = s2_6 = s2_7 = vdupq_n_u16(0);
    size_t num_iter = len >> 2;
    int rem = len & 3;
    for (size_t i = 0; i < num_iter; ++i) {
        uint8x16_t d0 = vld1q_u8(buf);
        uint8x16_t d1 = vld1q_u8(buf + 16);
        uint8x16_t d2 = vld1q_u8(buf + 32);
        uint8x16_t d3 = vld1q_u8(buf + 48);
        vst1q_u8(dst, d0);
        vst1q_u8(dst + 16, d1);
        vst1q_u8(dst + 32, d2);
        vst1q_u8(dst + 48, d3);
        dst += 64;
        uint16x8x2_t hsum, hsum_fold;
        hsum.val[0] = vpaddlq_u8(d0);
        hsum.val[1] = vpaddlq_u8(d1);
        hsum_fold.val[0] = vpadalq_u8(hsum.val[0], d2);
        hsum_fold.val[1] = vpadalq_u8(hsum.val[1], d3);
        adacc = vpadalq_u16(adacc, hsum_fold.val[0]);
        s3acc = vaddq_u32(s3acc, adacc_prev);
        adacc = vpadalq_u16(adacc, hsum_fold.val[1]);
        s2_0 = vaddw_u8(s2_0, vget_low_u8(d0));
        s2_1 = vaddw_high_u8(s2_1, d0);
        s2_2 = vaddw_u8(s2_2, vget_low_u8(d1));
        s2_3 = vaddw_high_u8(s2_3, d1);
        s2_4 = vaddw_u8(s2_4, vget_low_u8(d2));
        s2_5 = vaddw_high_u8(s2_5, d2);
        s2_6 = vaddw_u8(s2_6, vget_low_u8(d3));
        s2_7 = vaddw_high_u8(s2_7, d3);
        adacc_prev = adacc;
        buf += 64;
    }
    s3acc = vshlq_n_u32(s3acc, 6);
    if (rem) {
        uint32x4_t s3acc_0 = vdupq_n_u32(0);
        while (rem--) {
            uint8x16_t d0 = vld1q_u8(buf);
            vst1q_u8(dst, d0);
            dst += 16;
            uint16x8_t adler;
            adler = vpaddlq_u8(d0);
            s2_6 = vaddw_u8(s2_6, vget_low_u8(d0));
            s2_7 = vaddw_high_u8(s2_7, d0);
            adacc = vpadalq_u16(adacc, adler);
            s3acc_0 = vaddq_u32(s3acc_0, adacc_prev);
            adacc_prev = adacc;
            buf += 16;
        }
        s3acc_0 = vshlq_n_u32(s3acc_0, 4);
        s3acc = vaddq_u32(s3acc_0, s3acc);
    }
    uint16x8x4_t t0_t3 = vld1q_u16_x4(taps);
    uint16x8x4_t t4_t7 = vld1q_u16_x4(taps + 32);
    s2acc = vmlal_high_u16(s2acc, t0_t3.val[0], s2_0);
    s2acc_0 = vmlal_u16(s2acc_0, vget_low_u16(t0_t3.val[0]), vget_low_u16(s2_0));
    s2acc_1 = vmlal_high_u16(s2acc_1, t0_t3.val[1], s2_1);
    s2acc_2 = vmlal_u16(s2acc_2, vget_low_u16(t0_t3.val[1]), vget_low_u16(s2_1));
    s2acc = vmlal_high_u16(s2acc, t0_t3.val[2], s2_2);
    s2acc_0 = vmlal_u16(s2acc_0, vget_low_u16(t0_t3.val[2]), vget_low_u16(s2_2));
    s2acc_1 = vmlal_high_u16(s2acc_1, t0_t3.val[3], s2_3);
    s2acc_2 = vmlal_u16(s2acc_2, vget_low_u16(t0_t3.val[3]), vget_low_u16(s2_3));
    s2acc = vmlal_high_u16(s2acc, t4_t7.val[0], s2_4);
    s2acc_0 = vmlal_u16(s2acc_0, vget_low_u16(t4_t7.val[0]), vget_low_u16(s2_4));
    s2acc_1 = vmlal_high_u16(s2acc_1, t4_t7.val[1], s2_5);
    s2acc_2 = vmlal_u16(s2acc_2, vget_low_u16(t4_t7.val[1]), vget_low_u16(s2_5));
    s2acc = vmlal_high_u16(s2acc, t4_t7.val[2], s2_6);
    s2acc_0 = vmlal_u16(s2acc_0, vget_low_u16(t4_t7.val[2]), vget_low_u16(s2_6));
    s2acc_1 = vmlal_high_u16(s2acc_1, t4_t7.val[3], s2_7);
    s2acc_2 = vmlal_u16(s2acc_2, vget_low_u16(t4_t7.val[3]), vget_low_u16(s2_7));
    s2acc = vaddq_u32(s2acc_0, s2acc);
    s2acc_2 = vaddq_u32(s2acc_1, s2acc_2);
    s2acc = vaddq_u32(s2acc, s2acc_2);
    uint32x2_t adacc2, s2acc2, as;
    s2acc = vaddq_u32(s2acc, s3acc);
    adacc2 = vpadd_u32(vget_low_u32(adacc), vget_high_u32(adacc));
    s2acc2 = vpadd_u32(vget_low_u32(s2acc), vget_high_u32(s2acc));
    as = vpadd_u32(adacc2, s2acc2);
    s[0] = vget_lane_u32(as, 0);
    s[1] = vget_lane_u32(as, 1);
}

static inline void NEON_accum32(uint32_t *s, const uint8_t *buf, size_t len) {
    uint32x4_t adacc = vdupq_n_u32(0);
    uint32x4_t s2acc = vdupq_n_u32(0);
    uint32x4_t s2acc_0 = vdupq_n_u32(0);
    uint32x4_t s2acc_1 = vdupq_n_u32(0);
    uint32x4_t s2acc_2 = vdupq_n_u32(0);
    adacc = vsetq_lane_u32(s[0], adacc, 0);
    s2acc = vsetq_lane_u32(s[1], s2acc, 0);
    uint32x4_t s3acc = vdupq_n_u32(0);
    uint32x4_t adacc_prev = adacc;
    uint16x8_t s2_0, s2_1, s2_2, s2_3;
    s2_0 = s2_1 = s2_2 = s2_3 = vdupq_n_u16(0);
    uint16x8_t s2_4, s2_5, s2_6, s2_7;
    s2_4 = s2_5 = s2_6 = s2_7 = vdupq_n_u16(0);
    size_t num_iter = len >> 2;
    int rem = len & 3;
    for (size_t i = 0; i < num_iter; ++i) {
        uint8x16x4_t d0_d3 = vld1q_u8_x4(buf);
        uint16x8x2_t hsum, hsum_fold;
        hsum.val[0] = vpaddlq_u8(d0_d3.val[0]);
        hsum.val[1] = vpaddlq_u8(d0_d3.val[1]);
        hsum_fold.val[0] = vpadalq_u8(hsum.val[0], d0_d3.val[2]);
        hsum_fold.val[1] = vpadalq_u8(hsum.val[1], d0_d3.val[3]);
        adacc = vpadalq_u16(adacc, hsum_fold.val[0]);
        s3acc = vaddq_u32(s3acc, adacc_prev);
        adacc = vpadalq_u16(adacc, hsum_fold.val[1]);
        s2_0 = vaddw_u8(s2_0, vget_low_u8(d0_d3.val[0]));
        s2_1 = vaddw_high_u8(s2_1, d0_d3.val[0]);
        s2_2 = vaddw_u8(s2_2, vget_low_u8(d0_d3.val[1]));
        s2_3 = vaddw_high_u8(s2_3, d0_d3.val[1]);
        s2_4 = vaddw_u8(s2_4, vget_low_u8(d0_d3.val[2]));
        s2_5 = vaddw_high_u8(s2_5, d0_d3.val[2]);
        s2_6 = vaddw_u8(s2_6, vget_low_u8(d0_d3.val[3]));
        s2_7 = vaddw_high_u8(s2_7, d0_d3.val[3]);
        adacc_prev = adacc;
        buf += 64;
    }
    s3acc = vshlq_n_u32(s3acc, 6);
    if (rem) {
        uint32x4_t s3acc_0 = vdupq_n_u32(0);
        while (rem--) {
            uint8x16_t d0 = vld1q_u8(buf);
            uint16x8_t adler;
            adler = vpaddlq_u8(d0);
            s2_6 = vaddw_u8(s2_6, vget_low_u8(d0));
            s2_7 = vaddw_high_u8(s2_7, d0);
            adacc = vpadalq_u16(adacc, adler);
            s3acc_0 = vaddq_u32(s3acc_0, adacc_prev);
            adacc_prev = adacc;
            buf += 16;
        }
        s3acc_0 = vshlq_n_u32(s3acc_0, 4);
        s3acc = vaddq_u32(s3acc_0, s3acc);
    }
    uint16x8x4_t t0_t3 = vld1q_u16_x4(taps);
    uint16x8x4_t t4_t7 = vld1q_u16_x4(taps + 32);
    s2acc = vmlal_high_u16(s2acc, t0_t3.val[0], s2_0);
    s2acc_0 = vmlal_u16(s2acc_0, vget_low_u16(t0_t3.val[0]), vget_low_u16(s2_0));
    s2acc_1 = vmlal_high_u16(s2acc_1, t0_t3.val[1], s2_1);
    s2acc_2 = vmlal_u16(s2acc_2, vget_low_u16(t0_t3.val[1]), vget_low_u16(s2_1));
    s2acc = vmlal_high_u16(s2acc, t0_t3.val[2], s2_2);
    s2acc_0 = vmlal_u16(s2acc_0, vget_low_u16(t0_t3.val[2]), vget_low_u16(s2_2));
    s2acc_1 = vmlal_high_u16(s2acc_1, t0_t3.val[3], s2_3);
    s2acc_2 = vmlal_u16(s2acc_2, vget_low_u16(t0_t3.val[3]), vget_low_u16(s2_3));
    s2acc = vmlal_high_u16(s2acc, t4_t7.val[0], s2_4);
    s2acc_0 = vmlal_u16(s2acc_0, vget_low_u16(t4_t7.val[0]), vget_low_u16(s2_4));
    s2acc_1 = vmlal_high_u16(s2acc_1, t4_t7.val[1], s2_5);
    s2acc_2 = vmlal_u16(s2acc_2, vget_low_u16(t4_t7.val[1]), vget_low_u16(s2_5));
    s2acc = vmlal_high_u16(s2acc, t4_t7.val[2], s2_6);
    s2acc_0 = vmlal_u16(s2acc_0, vget_low_u16(t4_t7.val[2]), vget_low_u16(s2_6));
    s2acc_1 = vmlal_high_u16(s2acc_1, t4_t7.val[3], s2_7);
    s2acc_2 = vmlal_u16(s2acc_2, vget_low_u16(t4_t7.val[3]), vget_low_u16(s2_7));
    s2acc = vaddq_u32(s2acc_0, s2acc);
    s2acc_2 = vaddq_u32(s2acc_1, s2acc_2);
    s2acc = vaddq_u32(s2acc, s2acc_2);
    uint32x2_t adacc2, s2acc2, as;
    s2acc = vaddq_u32(s2acc, s3acc);
    adacc2 = vpadd_u32(vget_low_u32(adacc), vget_high_u32(adacc));
    s2acc2 = vpadd_u32(vget_low_u32(s2acc), vget_high_u32(s2acc));
    as = vpadd_u32(adacc2, s2acc2);
    s[0] = vget_lane_u32(as, 0);
    s[1] = vget_lane_u32(as, 1);
}

static inline void NEON_handle_tail(uint32_t *pair, const uint8_t *buf, size_t len) {
    unsigned int i;
    for (i = 0; i < len; ++i) {
        pair[0] += buf[i];
        pair[1] += pair[0];
    }
}

static inline uint32_t adler32_neon(uint32_t adler, const uint8_t *src, size_t len) {
    uint32_t sum2 = (adler >> 16) & 0xffff;
    adler &= 0xffff;
    if (UNLIKELY(len == 1))
        return adler32_len_1(adler, src, sum2);
    if (UNLIKELY(len < 16))
        return adler32_len_16(adler, src, len, sum2);
    uint32_t pair[2];
    int n = NMAX;
    unsigned int done = 0;
    pair[0] = adler;
    pair[1] = sum2;
    unsigned int align_offset = ((uintptr_t)src & 31);
    unsigned int align_adj = (align_offset) ? 32 - align_offset : 0;
    if (align_offset && len >= (16 + align_adj)) {
        NEON_handle_tail(pair, src, align_adj);
        n -= align_adj;
        done += align_adj;
    } else {
        align_adj = 0;
    }
    while (done < len) {
        int remaining = (int)(len - done);
        n = MIN(remaining, (done == align_adj) ? n : NMAX);
        if (n < 16)
            break;
        NEON_accum32(pair, src + done, n >> 4);
        pair[0] %= BASE;
        pair[1] %= BASE;
        int actual_nsums = (n >> 4) << 4;
        done += actual_nsums;
    }
    if (done < len) {
        NEON_handle_tail(pair, (src + done), len - done);
        pair[0] %= BASE;
        pair[1] %= BASE;
    }
    return (pair[1] << 16) | pair[0];
}

AVAdler av_adler32_update(AVAdler adler, const uint8_t *src, size_t len) {
    return adler32_neon(adler, src, len);
}

#endif

#else

#define BASE 65521L /* largest prime smaller than 65536 */
#define DO1(buf)  { s1 += *buf++; s2 += s1; }
#define DO4(buf)  DO1(buf); DO1(buf); DO1(buf); DO1(buf);
#define DO16(buf) DO4(buf); DO4(buf); DO4(buf); DO4(buf);

AVAdler av_adler32_update(AVAdler adler, const uint8_t *buf, size_t len)
{
    unsigned long s1 = adler & 0xffff;
    unsigned long s2 = adler >> 16;

    while (len > 0) {
#if HAVE_FAST_64BIT && HAVE_FAST_UNALIGNED && !CONFIG_SMALL
        unsigned len2 = FFMIN((len-1) & ~7, 23*8);
        if (len2) {
            uint64_t a1= 0;
            uint64_t a2= 0;
            uint64_t b1= 0;
            uint64_t b2= 0;
            len -= len2;
            s2 += s1*len2;
            while (len2 >= 8) {
                uint64_t v = AV_RN64(buf);
                a2 += a1;
                b2 += b1;
                a1 +=  v    &0x00FF00FF00FF00FF;
                b1 += (v>>8)&0x00FF00FF00FF00FF;
                len2 -= 8;
                buf+=8;
            }

            //We combine the 8 interleaved adler32 checksums without overflows
            //Decreasing the number of iterations would allow below code to be
            //simplified but would likely be slower due to the fewer iterations
            //of the inner loop
            s1 += ((a1+b1)*0x1000100010001)>>48;
            s2 += ((((a2&0xFFFF0000FFFF)+(b2&0xFFFF0000FFFF)+((a2>>16)&0xFFFF0000FFFF)+((b2>>16)&0xFFFF0000FFFF))*0x800000008)>>32)
#if HAVE_BIGENDIAN
                 + 2*((b1*0x1000200030004)>>48)
                 +   ((a1*0x1000100010001)>>48)
                 + 2*((a1*0x0000100020003)>>48);
#else
                 + 2*((a1*0x4000300020001)>>48)
                 +   ((b1*0x1000100010001)>>48)
                 + 2*((b1*0x3000200010000)>>48);
#endif
        }
#else
        while (len > 4  && s2 < (1U << 31)) {
            DO4(buf);
            len -= 4;
        }
#endif
        DO1(buf); len--;
        s1 %= BASE;
        s2 %= BASE;
    }
    return (s2 << 16) | s1;
}

#endif
