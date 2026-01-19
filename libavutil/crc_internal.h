/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_CRC_INTERNAL_H
#define AVUTIL_CRC_INTERNAL_H

#include <stdint.h>
#include "libavutil/reverse.h"

static inline uint64_t reverse(uint64_t p, unsigned int deg)
{
    return __builtin_elementwise_bitreverse(p) >> (64 - deg);
}

static inline uint64_t xnmodp(unsigned n, uint64_t poly, unsigned deg, uint64_t *div, int bitreverse)
{
    uint64_t mod, mask, high;

    if (n < deg) {
        *div = 0;
        return poly;
    }
    mask = ((uint64_t)1 << deg) - 1;
    poly &= mask;
    mod = poly;
    *div = 1;
    deg--;
    while (--n > deg) {
        high = (mod >> deg) & 1;
        *div = (*div << 1) | high;
        mod <<= 1;
        if (high)
            mod ^= poly;
    }
    uint64_t ret = mod & mask;
    if (bitreverse) {
        *div = reverse(*div, deg) << 1;
        return reverse(ret, deg) << 1;
    }
    return ret;
}

#endif /* AVUTIL_CRC_INTERNAL_H */
