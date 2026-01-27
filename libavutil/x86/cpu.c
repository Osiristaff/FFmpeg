/*
 * CPU detection code, extracted from mmx.h
 * (c)1997-99 by H. Dietz and R. Fisher
 * Converted to C and improved by Fabrice Bellard.
 *
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

#include <stdlib.h>
#include <string.h>

#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/cpu.h"
#include "libavutil/cpu_internal.h"

#if HAVE_X86ASM

#define cpuid(index, eax, ebx, ecx, edx)        \
    ff_cpu_cpuid(index, &eax, &ebx, &ecx, &edx)

#define xgetbv(index, eax, edx)                 \
    ff_cpu_xgetbv(index, &eax, &edx)

#elif HAVE_INLINE_ASM

/* ebx saving is necessary for PIC. gcc seems unable to see it alone */
#define cpuid(index, eax, ebx, ecx, edx)                        \
    __asm__ volatile (                                          \
        "mov    %%"FF_REG_b", %%"FF_REG_S" \n\t"                \
        "cpuid                       \n\t"                      \
        "xchg   %%"FF_REG_b", %%"FF_REG_S                       \
        : "=a" (eax), "=S" (ebx), "=c" (ecx), "=d" (edx)        \
        : "0" (index), "2"(0))

#define xgetbv(index, eax, edx)                                 \
    __asm__ (".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c" (index))

#define get_eflags(x)                           \
    __asm__ volatile ("pushfl     \n"           \
                      "pop    %0  \n"           \
                      : "=r"(x))

#define set_eflags(x)                           \
    __asm__ volatile ("push    %0 \n"           \
                      "popfl      \n"           \
                      :: "r"(x))

#endif /* HAVE_INLINE_ASM */

#if ARCH_X86_64

#define cpuid_test() 1

#elif HAVE_X86ASM

#define cpuid_test ff_cpu_cpuid_test

#elif HAVE_INLINE_ASM

static int cpuid_test(void)
{
    x86_reg a, c;

    /* Check if CPUID is supported by attempting to toggle the ID bit in
     * the EFLAGS register. */
    get_eflags(a);
    set_eflags(a ^ 0x200000);
    get_eflags(c);

    return a != c;
}
#endif

/* Function to test if multimedia instructions are supported...  */
int ff_get_cpu_flags_x86(void)
{
    int rval = AV_CPU_FLAG_SSE2|AV_CPU_FLAG_CMOV|AV_CPU_FLAG_MMX;

    #ifdef __SSE3__
        rval |= AV_CPU_FLAG_SSE3;
    #endif
    #ifdef __SSSE3__
        rval |= AV_CPU_FLAG_SSSE3;
    #endif
    #ifdef __SSE4_1__
        rval |= AV_CPU_FLAG_SSE4;
    #endif
    #ifdef __SSE4_2__
        rval |= AV_CPU_FLAG_SSE42;
    #endif
    #ifdef __PCLMUL__
        rval |= AV_CPU_FLAG_CLMUL;
    #endif
    #ifdef __AES__
        rval |= AV_CPU_FLAG_AESNI;
    #endif
    #ifdef __AVX__
        rval |= AV_CPU_FLAG_AVX;
    #endif
    #ifdef __FMA__
        rval |= AV_CPU_FLAG_FMA3;
    #endif
    #ifdef __AVX2__
        rval |= AV_CPU_FLAG_AVX2;
    #endif
    #ifdef __AVX512F__
        rval |= AV_CPU_FLAG_AVX512;
    #endif
    #ifdef __AVX512VBMI2__
        rval |= AV_CPU_FLAG_AVX512ICL;
    #endif
    #ifdef __BMI__
        rval |= AV_CPU_FLAG_BMI1;
    #endif
    #ifdef __BMI2__
        rval |= AV_CPU_FLAG_BMI2;
    #endif

#ifdef cpuid

    int eax, ebx, ecx, edx;
    int max_std_level, max_ext_level, std_caps = 0, ext_caps = 0;
    int family = 0, model = 0;
    union { int i[3]; char c[12]; } vendor;
    int xcr0_lo = 0, xcr0_hi = 0;

    if (!cpuid_test())
        return 0; /* CPUID not supported */

    enum {
        LEAF1 =
            AV_CPU_FLAG_CMOV       |
            AV_CPU_FLAG_MMX        |
            AV_CPU_FLAG_SSE2       |
            AV_CPU_FLAG_SSE3       |
            AV_CPU_FLAG_SSSE3      |
            AV_CPU_FLAG_SSE4       |
            AV_CPU_FLAG_SSE42      |
            AV_CPU_FLAG_AESNI      |
            AV_CPU_FLAG_CLMUL      |
            AV_CPU_FLAG_AVX        |
            AV_CPU_FLAG_FMA3,

        LEAF7 =
            AV_CPU_FLAG_AVX2       |
            AV_CPU_FLAG_AVX512     |
            AV_CPU_FLAG_AVX512ICL  |
            AV_CPU_FLAG_BMI1       |
            AV_CPU_FLAG_BMI2,
    };

    const int static_missing = (LEAF1 | LEAF7) & ~rval;

    if(static_missing & LEAF1){
        cpuid(1, eax, ebx, ecx, std_caps);
        if (static_missing & AV_CPU_FLAG_SSE3){
            if (ecx & 1){
                rval |= AV_CPU_FLAG_SSE3;
            }
        }
        if (static_missing & AV_CPU_FLAG_CLMUL){
            if (ecx & 0x2){
                rval |= AV_CPU_FLAG_CLMUL;
            }
        }
        if (static_missing & AV_CPU_FLAG_SSSE3){
            if (ecx & 0x00000200 ){
                rval |= AV_CPU_FLAG_SSSE3;
            }
        }
        if (static_missing & AV_CPU_FLAG_SSE4){
            if (ecx & 0x00080000 ){
                rval |= AV_CPU_FLAG_SSE4;
            }
        }
        if (static_missing & AV_CPU_FLAG_SSE42){
            if (ecx & 0x00100000 ){
                rval |= AV_CPU_FLAG_SSE42;
            }
        }
        if (static_missing & AV_CPU_FLAG_AESNI){
            if (ecx & 0x02000000 ){
                rval |= AV_CPU_FLAG_AESNI;
            }
        }
        if (static_missing & AV_CPU_FLAG_AVX){
            /* Check OXSAVE and AVX bits */
            if ((ecx & 0x18000000) == 0x18000000){
                /* Check for OS support */
                xgetbv(0, xcr0_lo, xcr0_hi);
                if ((xcr0_lo & 0x6) == 0x6){
                    rval |= AV_CPU_FLAG_AVX;
                    if (ecx & 0x00001000){
                        rval |= AV_CPU_FLAG_FMA3;
                    }
                }
            }
        }else{
            if (static_missing & AV_CPU_FLAG_FMA3){
                if (ecx & 0x00001000){
                    rval |= AV_CPU_FLAG_FMA3;
                }
            }
        }
    }

    if(static_missing & LEAF7){
        cpuid(7, eax, ebx, ecx, edx);
        if(static_missing & AV_CPU_FLAG_AVX2){
            if ((rval & AV_CPU_FLAG_AVX) && (ebx & 0x00000020)){
                rval |= AV_CPU_FLAG_AVX2;
                if(!(static_missing & AV_CPU_FLAG_AVX)){
                    xgetbv(0, xcr0_lo, xcr0_hi);
                }
                if ((xcr0_lo & 0xe0) == 0xe0){ /* OPMASK/ZMM state */
                    if ((ebx & 0xd0030000) == 0xd0030000){
                        rval |= AV_CPU_FLAG_AVX512;
                        if ((ebx & 0xd0200000) == 0xd0200000 && (ecx & 0x5f42) == 0x5f42){
                            rval |= AV_CPU_FLAG_AVX512ICL;
                        }
                    }
                }
            }
        }else{
            if(static_missing & AV_CPU_FLAG_AVX512){
                if(!(static_missing & AV_CPU_FLAG_AVX)){
                    xgetbv(0, xcr0_lo, xcr0_hi);
                }
                if ((xcr0_lo & 0xe0) == 0xe0){ /* OPMASK/ZMM state */
                    if ((ebx & 0xd0030000) == 0xd0030000){
                        rval |= AV_CPU_FLAG_AVX512;
                        if ((ebx & 0xd0200000) == 0xd0200000 && (ecx & 0x5f42) == 0x5f42){
                            rval |= AV_CPU_FLAG_AVX512ICL;
                        }
                    }
                }
            }else{
                if(static_missing & AV_CPU_FLAG_AVX512ICL){
                    if ((ebx & 0xd0200000) == 0xd0200000 && (ecx & 0x5f42) == 0x5f42){
                        rval |= AV_CPU_FLAG_AVX512ICL;
                    }
                }
            }
        }

        if(static_missing & AV_CPU_FLAG_BMI1){
            /* BMI1/2 don't need OS support */
            if (ebx & 0x00000008){
                rval |= AV_CPU_FLAG_BMI1;
                if (ebx & 0x00000100){
                    rval |= AV_CPU_FLAG_BMI2;
                }
            }
        }else{
            if(static_missing & AV_CPU_FLAG_BMI2){
                if (ebx & 0x00000100){
                    rval |= AV_CPU_FLAG_BMI2;
                }
            }
        }
    }

#endif /* cpuid */

    return rval;
}

size_t ff_get_cpu_max_align_x86(void)
{
    int flags = av_get_cpu_flags();

    if (EXTERNAL_AVX512(flags))
        return 64;
    if (EXTERNAL_AVX2(flags))
        return 32;
    if (EXTERNAL_SSE2(flags))
        return 16;

    return 8;
}
