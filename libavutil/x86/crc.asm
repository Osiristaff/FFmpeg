;*****************************************************************************
;* Copyright (c) 2025 Shreesh Adiga <16567adigashreesh@gmail.com>
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with FFmpeg; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86util.asm"

SECTION_RODATA
reverse_shuffle: db 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

partial_bytes_shuf_tab: db 255, 254, 253, 252, 251, 250, 249, 248,\
                           247, 246, 245, 244, 243, 242, 241, 240,\
                             0,   1,   2,   3,   4,   5,   6,   7,\
                             8,   9,  10,  11,  12,  13,  14,  15

SECTION .text

%macro FOLD_128_TO_64 4
; %1 LE ; %2 128 bit fold reg ; %3 pre-computed constant reg ; %4 tmp reg
%if %1 == 1
    mova      %4, %2
    pclmulqdq %2, %3, 0x00
    psrldq    %4, 8
    pxor      %2, %4
    mova      %4, %2
    psllq     %4, 32
    pclmulqdq %4, %3, 0x10
    pxor      %2, %4
%else
    movq      %4, %2
    pclmulqdq %2, %3, 0x11
    pslldq    %4, 4
    pxor      %4, %2
    mova      %2, %4
    pclmulqdq %4, %3, 0x01
    pxor      %2, %4
%endif
%endmacro

%macro FOLD_64_TO_32 4
; %1 LE ; %2 128 bit fold reg ; %3 pre-computed constant reg ; %4 tmp reg
%if %1 == 1
    pxor      %4, %4
    %if mmsize == 64
    vmovss    %4, %2, %4
    %else
    pblendw   %4, %2, 0xfc
    %endif
    mova      %2, %4
    pclmulqdq %4, %3, 0x00
    pxor      %4, %2
    pclmulqdq %4, %3, 0x10
    pxor      %2, %4
    pextrd   eax, %2, 2
%else
    mova      %4, %2
    pclmulqdq %2, %3, 0x00
    pclmulqdq %2, %3, 0x11
    pxor      %2, %4
    movd     eax, %2
    bswap    eax
%endif
%endmacro

%macro FOLD_SINGLE 4
; %1 temp ; %2 fold reg ; %3 pre-computed constants ; %4 input data block
%if mmsize == 64
    pclmulqdq  %1, %2, %3, 0x01
    pclmulqdq  %2, %2, %3, 0x10
    vpternlogq %2, %1, %4, 0x96
%else
    mova      %1, %2
    pclmulqdq %1, %3, 0x01
    pxor      %1, %4
    pclmulqdq %2, %3, 0x10
    pxor      %2, %1
%endif
%endmacro

%macro XMM_SHIFT_LEFT 4
; %1 xmm input reg ; %2 shift bytes amount ; %3 temp xmm register ; %4 temp gpr
    lea    %4, [partial_bytes_shuf_tab]
    movu   %3, [%4 + 16 - (%2)]
    pshufb %1, %3
%endmacro

%macro MEMCPY_0_15 6
; %1 dst ; %2 src ; %3 len ; %4, %5 temp gpr register; %6 done label
    cmp %3, 8
    jae .between_8_15
    cmp %3, 4
    jae .between_4_7
    cmp %3, 1
    ja .between_2_3
    jb %6
    mov  %4b, [%2]
    mov [%1], %4b
    jmp %6

.between_8_15:
%if ARCH_X86_64
    mov           %4q, [%2]
    mov           %5q, [%2 + %3 - 8]
    mov          [%1], %4q
    mov [%1 + %3 - 8], %5q
    jmp %6
%else
    xor            %5, %5
.copy4b:
        mov       %4d, [%2 + %5]
        mov [%1 + %5], %4d
        add        %5, 4
        lea        %4, [%5 + 4]
        cmp        %4, %3
        jb        .copy4b

    mov           %4d, [%2 + %3 - 4]
    mov [%1 + %3 - 4], %4d
    jmp %6
%endif
.between_4_7:
    mov           %4d, [%2]
    mov           %5d, [%2 + %3 - 4]
    mov          [%1], %4d
    mov [%1 + %3 - 4], %5d
    jmp %6
.between_2_3:
    mov           %4w, [%2]
    mov           %5w, [%2 + %3 - 2]
    mov          [%1], %4w
    mov [%1 + %3 - 2], %5w
    ; fall through, %6 label is expected to be next instruction
%endmacro

%macro VBROADCASTI32x4 3
; %1 dst reg ; %2 address for AVX512ICL ; %3 address for SSE4.2
    %if mmsize == 64
        vbroadcasti32x4 %1, [%2]
    %else
        movu            %1, [%3]
    %endif
%endmacro

%macro CRC 1
%define CTX r0+4
;-----------------------------------------------------------------------------------------------
; ff_crc[_le]_clmul(const uint8_t *ctx, uint32_t crc, const uint8_t *buffer, size_t length
;-----------------------------------------------------------------------------------------------
; %1 == 1 - LE format
%if mmsize == 64
    %if %1 == 1
    cglobal crc_le, 4, 6, 6+4*ARCH_X86_64, 0
    %else
    cglobal crc,    4, 6, 7+4*ARCH_X86_64, 0
    %endif
%else
    %if %1 == 1
    cglobal crc_le, 4, 6, 6+4*ARCH_X86_64, 0x10
    %else
    cglobal crc,    4, 6, 7+4*ARCH_X86_64, 0x10
    %endif
%endif

%if ARCH_X86_32
    %define m10 m6
%endif

%if %1 == 0
    VBROADCASTI32x4 m10, reverse_shuffle, reverse_shuffle
%endif

%if mmsize == 64
    pxor    m4, m4
%endif
    movd   xm4, r1d

%if ARCH_X86_32
    ; skip 4x unrolled loop due to only 8 XMM reg being available in X86_32
    jmp            .less_than_4x_mmsize
%else
    cmp             r3, 4 * mmsize
    jb             .less_than_4x_mmsize
    movu            m1, [r2 + 0 * mmsize]
    movu            m3, [r2 + 1 * mmsize]
    movu            m2, [r2 + 2 * mmsize]
    movu            m0, [r2 + 3 * mmsize]
    pxor            m1, m4
%if %1 == 0
    pshufb          m0, m10
    pshufb          m1, m10
    pshufb          m2, m10
    pshufb          m3, m10
%endif
    mov             r4, 4 * mmsize
    cmp             r3, 8 * mmsize
    jb             .reduce_4x_to_1
    VBROADCASTI32x4 m4, CTX + 64, CTX

.fold_4x_loop:
        movu        m6, [r2 + r4 + 0 * mmsize]
        movu        m7, [r2 + r4 + 1 * mmsize]
        movu        m8, [r2 + r4 + 2 * mmsize]
        movu        m9, [r2 + r4 + 3 * mmsize]
%if %1 == 0
        pshufb      m6, m10
        pshufb      m7, m10
        pshufb      m8, m10
        pshufb      m9, m10
%endif
        FOLD_SINGLE m5, m1, m4, m6
        FOLD_SINGLE m5, m3, m4, m7
        FOLD_SINGLE m5, m2, m4, m8
        FOLD_SINGLE m5, m0, m4, m9
        add         r4, 4 * mmsize
        lea         r5, [r4 + 4 * mmsize]
        cmp         r5, r3
        jbe        .fold_4x_loop

.reduce_4x_to_1:
    VBROADCASTI32x4 m4, CTX, CTX + 16
    FOLD_SINGLE     m5,  m1, m4, m3
    FOLD_SINGLE     m5,  m1, m4, m2
    FOLD_SINGLE     m5,  m1, m4, m0
%endif

.fold_1x_pre:
    lea  r5, [r4 + mmsize]
    cmp  r5, r3
%if mmsize == 64
    ja  .fold_zmm_to_xmm
%else
    ja  .partial_block
%endif

.fold_1x_loop:
        movu        m2, [r2 + r4]
%if %1 == 0
        pshufb      m2, m10
%endif
        FOLD_SINGLE m5, m1, m4, m2
        add         r4, mmsize
        lea         r5, [r4 + mmsize]
        cmp         r5, r3
        jbe        .fold_1x_loop

%if mmsize == 64
.fold_zmm_to_xmm:
    movu            xm4, [CTX + 16]
    vextracti32x4   xm0,  m1, 1
    vextracti32x4   xm2,  m1, 2
    vextracti32x4   xm3,  m1, 3
    FOLD_SINGLE     xm5, xm1, xm4, xm0
    FOLD_SINGLE     xm5, xm1, xm4, xm2
    FOLD_SINGLE     xm5, xm1, xm4, xm3

.fold_16b_pre:
    lea r5, [r4 + 16]
    cmp r5, r3
    ja .partial_block

.fold_16b_loop:
        movu        xm2, [r2 + r4]
%if %1 == 0
        pshufb      xm2, xm10
%endif
        FOLD_SINGLE xm5, xm1, xm4, xm2
        add          r4, 16
        lea          r5, [r4 + 16]
        cmp          r5, r3
        jbe         .fold_16b_loop
%endif

.partial_block:
    cmp             r4, r3
    jae           .reduce_128_to_64
    movu           xm2, [r2 + r3 - 16]
    and             r3, 0xf
    lea             r4, [partial_bytes_shuf_tab]
    movu           xm0, [r3 + r4]
%if %1 == 0
    pshufb         xm1, xm10
%endif
    mova           xm3, xm1
%if mmsize == 64
    mova           xm5, xm0
    vpternlogq     xm5, xm0, xm0, 0xf ; xm5 = ~xm0
    vpmovb2m        k1, xm0
    pshufb         xm3, xm5
    vpblendmb  xm2{k1}, xm2, xm3
%else
    pcmpeqd        xm5, xm5 ; m5 = _mm_set1_epi8(0xff)
    pxor           xm5, xm0
    pshufb         xm3, xm5
    pblendvb       xm2, xm3, xm0
%endif
    pshufb         xm1, xm0
%if %1 == 0
    pshufb         xm1, xm10
    pshufb         xm2, xm10
%endif
    FOLD_SINGLE    xm5, xm1, xm4, xm2

.reduce_128_to_64:
    movu           xm4, [CTX + 32]
    FOLD_128_TO_64  %1, xm1, xm4, xm5
.reduce_64_to_32:
    movu           xm4, [CTX + 48]
    FOLD_64_TO_32   %1, xm1, xm4, xm5
    RET

.less_than_4x_mmsize:
    cmp             r3, mmsize
    jb             .less_than_mmsize
    movu            m1, [r2]
    pxor            m1, m4
%if %1 == 0
    pshufb          m1, m10
%endif
    mov             r4, mmsize
    VBROADCASTI32x4 m4, CTX, CTX + 16
    jmp            .fold_1x_pre

.less_than_mmsize:
%if mmsize == 64
    cmp     r3, 16
    jb    .less_than_16bytes
    movu   xm1, [r2]
    pxor   xm1, xm4
%if %1 == 0
    pshufb xm1, xm10
%endif
    mov     r4, 16
    movu   xm4, [CTX + 16]
    jmp   .fold_16b_pre

.less_than_16bytes:
    mov                  r4d, -1
    shlx                 r4d, r4d, r3d
    not                  r4d
    kmovw                 k1, r4d
    vmovdqu8      xm1{k1}{z}, [r2]
%else
    pxor                  m1, m1
    movu               [rsp], m1
    MEMCPY_0_15          rsp, r2, r3, r1, r4, .memcpy_done

.memcpy_done:
    movu                  m1, [rsp]
%endif

    pxor                 xm1, xm4
    cmp                   r3, 5
    jb                  .less_than_5bytes
    XMM_SHIFT_LEFT       xm1, (16 - r3), xm2, r4
%if %1 == 0
    pshufb               xm1, xm10
%endif
    jmp                 .reduce_128_to_64

.less_than_5bytes:
%if %1 == 0
    XMM_SHIFT_LEFT       xm1, (4 - r3), xm2, r4
    movq                xm10, [reverse_shuffle + 8] ; 0x0001020304050607
    pshufb               xm1, xm10
%else
    XMM_SHIFT_LEFT       xm1, (8 - r3), xm2, r4
%endif
    jmp                 .reduce_64_to_32

%endmacro

INIT_XMM clmul
CRC 0
CRC 1

INIT_ZMM avx512icl
CRC 0
CRC 1
