/***
  This file is part of PulseAudio.

  Copyright 2012 Peter Meerwald <p.meerwald@bct-electronic.com>

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/macro.h>
#include <pulsecore/endianmacros.h>

#include "cpu-arm.h"
#include "sconv.h"

#include <math.h>
#include <arm_neon.h>

static void pa_sconv_s16le_from_f32ne_neon(unsigned n, const float *src, int16_t *dst) {
    unsigned i = n & 3;

    __asm__ __volatile__ (
        "movs       %[n], %[n], lsr #2      \n\t"
        "beq        2f                      \n\t"

        "1:                                 \n\t"
        "vld1.32    {q0}, [%[src]]!         \n\t"
        "vcvt.s32.f32 q0, q0, #31           \n\t" /* s32<-f32 as 16:16 fixed-point, with implicit multiplication by 32768 */
        "vqrshrn.s32 d0, q0, #16            \n\t" /* shift, round, narrow */
        "subs       %[n], %[n], #1          \n\t"
        "vst1.16    {d0}, [%[dst]]!         \n\t"
        "bgt        1b                      \n\t"

        "2:                                 \n\t"

        : [dst] "+r" (dst), [src] "+r" (src), [n] "+r" (n) /* output operands (or input operands that get modified) */
        : /* input operands */
        : "memory", "cc", "q0" /* clobber list */
    );

    /* leftovers */
    while (i--) {
        *dst++ = (int16_t) PA_CLAMP_UNLIKELY(lrintf(*src * (1 << 15)), -0x8000, 0x7FFF);
        src++;
    }
}

static void pa_sconv_s16le_to_f32ne_neon(unsigned n, const int16_t *src, float *dst) {
    unsigned i = n & 3;
    const float invscale = 1.0f / (1 << 15);

    __asm__ __volatile__ (
        "movs       %[n], %[n], lsr #2      \n\t"
        "beq        2f                      \n\t"

        "1:                                 \n\t"
        "vld1.16    {d0}, [%[src]]!         \n\t"
        "vmovl.s16  q0, d0                  \n\t" /* widen */
        "vcvt.f32.s32 q0, q0, #15           \n\t" /* f32<-s32 and divide by (1<<15) */
        "subs       %[n], %[n], #1          \n\t"
        "vst1.32    {q0}, [%[dst]]!         \n\t"
        "bgt        1b                      \n\t"

        "2:                                 \n\t"

        : [dst] "+r" (dst), [src] "+r" (src), [n] "+r" (n) /* output operands (or input operands that get modified) */
        : /* input operands */
        : "memory", "cc", "q0" /* clobber list */
    );

    /* leftovers */
    while (i--) {
        *dst++ = *src++ * invscale;
    }
}

void pa_convert_func_init_neon(pa_cpu_arm_flag_t flags) {
    pa_log_info("Initialising ARM NEON optimized conversions.");
    pa_set_convert_from_float32ne_function(PA_SAMPLE_S16LE, (pa_convert_func_t) pa_sconv_s16le_from_f32ne_neon);
    pa_set_convert_to_float32ne_function(PA_SAMPLE_S16LE, (pa_convert_func_t) pa_sconv_s16le_to_f32ne_neon);
#ifndef WORDS_BIGENDIAN
    pa_set_convert_from_s16ne_function(PA_SAMPLE_FLOAT32LE, (pa_convert_func_t) pa_sconv_s16le_to_f32ne_neon);
    pa_set_convert_to_s16ne_function(PA_SAMPLE_FLOAT32LE, (pa_convert_func_t) pa_sconv_s16le_from_f32ne_neon);
#endif
}
