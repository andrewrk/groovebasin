/***
  This file is part of PulseAudio.

  Copyright 2013 Peter Meerwald <pmeerw@pmeerw.net>

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
#include <pulsecore/sample-util.h>

#include "cpu-arm.h"
#include "mix.h"

#include <arm_neon.h>

static pa_do_mix_func_t fallback;

/* special case: mix s16ne streams, 2 channels each */
static void pa_mix_ch2_s16ne_neon(pa_mix_info streams[], unsigned nstreams, uint8_t *data, unsigned length) {
    const unsigned mask = sizeof(int16_t) * 8 - 1;
    const uint8_t *end = data + (length & ~mask);

    while (data < end) {
        int32x4_t sum0, sum1;
        unsigned i;

        __asm__ __volatile__ (
            "veor.s32 %q[sum0], %q[sum0]     \n\t"
            "veor.s32 %q[sum1], %q[sum1]     \n\t"
            : [sum0] "=w" (sum0), [sum1] "=w" (sum1)
            :
            : "cc" /* clobber list */
        );

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv0 = m->linear[0].i;
            int32_t cv1 = m->linear[1].i;

            __asm__ __volatile__ (
                "vld2.s16    {d0,d2}, [%[ptr]]!      \n\t"
                "vmov.s32    d4[0], %[cv0]           \n\t"
                "vmov.s32    d4[1], %[cv1]           \n\t"
                "vshll.s16   q0, d0, #15             \n\t"
                "vshll.s16   q1, d2, #15             \n\t"
                "vqdmulh.s32 q0, q0, d4[0]           \n\t"
                "vqdmulh.s32 q1, q1, d4[1]           \n\t"
                "vqadd.s32   %q[sum0], %q[sum0], q0  \n\t"
                "vqadd.s32   %q[sum1], %q[sum1], q1  \n\t"
                : [ptr] "+r" (m->ptr), [sum0] "+w" (sum0), [sum1] "+w" (sum1)
                : [cv0] "r" (cv0), [cv1] "r" (cv1)
                : "memory", "cc", "q0", "q1", "d4" /* clobber list */
            );
        }

        __asm__ __volatile__ (
            "vqmovn.s32 d0, %q[sum0]         \n\t"
            "vqmovn.s32 d1, %q[sum1]         \n\t"
            "vst2.s16   {d0,d1}, [%[data]]!  \n\t"
            : [data] "+r" (data)
            : [sum0] "w" (sum0), [sum1] "w" (sum1)
            : "memory", "cc", "q0" /* clobber list */
        );
    }

    fallback(streams, nstreams, 2, data, length & mask);
}

/* special case: mix 2 s16ne streams, 1 channel each */
static void pa_mix2_ch1_s16ne_neon(pa_mix_info streams[], int16_t *data, unsigned length) {
    const int16_t *ptr0 = streams[0].ptr;
    const int16_t *ptr1 = streams[1].ptr;

    int32x4_t sv0, sv1;
    __asm__ __volatile__ (
        "vdup.s32    %q[sv0], %[lin0]        \n\t"
        "vdup.s32    %q[sv1], %[lin1]        \n\t"
        : [sv0] "=w" (sv0), [sv1] "=w" (sv1)
        : [lin0] "r" (streams[0].linear[0]), [lin1] "r" (streams[1].linear[0])
        : /* clobber list */
    );

    length /= sizeof(int16_t);
    for (; length >= 4; length -= 4) {
        __asm__ __volatile__ (
            "vld1.s16    d0, [%[ptr0]]!      \n\t"
            "vld1.s16    d2, [%[ptr1]]!      \n\t"
            "vshll.s16   q0, d0, #15         \n\t"
            "vshll.s16   q1, d2, #15         \n\t"
            "vqdmulh.s32 q0, q0, %q[sv0]     \n\t"
            "vqdmulh.s32 q1, q1, %q[sv1]     \n\t"
            "vqadd.s32   q0, q0, q1          \n\t"
            "vqmovn.s32  d0, q0              \n\t"
            "vst1.s16    d0, [%[data]]!      \n\t"
            : [ptr0] "+r" (ptr0), [ptr1] "+r" (ptr1), [data] "+r" (data)
            : [sv0] "w" (sv0), [sv1] "w" (sv1)
            : "memory", "cc", "q0", "q1" /* clobber list */
        );
    }

    for (; length > 0; length--) {
        int32_t sum = pa_mult_s16_volume(*ptr0++, streams[0].linear[0].i);
        sum += pa_mult_s16_volume(*ptr1++, streams[1].linear[0].i);
        *data++ = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
    }
}

/* special case: mix 2 s16ne streams, 2 channel each */
static void pa_mix2_ch2_s16ne_neon(pa_mix_info streams[], int16_t *data, unsigned length) {
    const int16_t *ptr0 = streams[0].ptr;
    const int16_t *ptr1 = streams[1].ptr;

    int32x4_t sv0, sv1;
    __asm__ __volatile__ (
        "vld1.s32 d0, [%[lin0]]              \n\t"
        "vmov.s32 d1, d0                     \n\t"
        "vmov.s32 %q[sv0], q0                \n\t"
        "vld1.s32 d0, [%[lin1]]              \n\t"
        "vmov.s32 d1, d0                     \n\t"
        "vmov.s32 %q[sv1], q0                \n\t"
        : [sv0] "=w" (sv0), [sv1] "=w" (sv1)
        : [lin0] "r" (streams[0].linear), [lin1] "r" (streams[1].linear)
        : "q0" /* clobber list */
    );

    length /= sizeof(int16_t);
    for (; length >= 4; length -= 4) {
        __asm__ __volatile__ (
            "vld1.s16    d0, [%[ptr0]]!      \n\t"
            "vld1.s16    d2, [%[ptr1]]!      \n\t"
            "vshll.s16   q0, d0, #15         \n\t"
            "vshll.s16   q1, d2, #15         \n\t"
            "vqdmulh.s32 q0, q0, %q[sv0]     \n\t"
            "vqdmulh.s32 q1, q1, %q[sv1]     \n\t"
            "vqadd.s32   q0, q0, q1          \n\t"
            "vqmovn.s32  d0, q0              \n\t"
            "vst1.s16    d0, [%[data]]!      \n\t"
            : [ptr0] "+r" (ptr0), [ptr1] "+r" (ptr1), [data] "+r" (data)
            : [sv0] "w" (sv0), [sv1] "w" (sv1)
            : "memory", "cc", "q0", "q1" /* clobber list */
        );
    }

    if (length > 0) {
        int32_t sum;

        sum = pa_mult_s16_volume(*ptr0++, streams[0].linear[0].i);
        sum += pa_mult_s16_volume(*ptr1++, streams[1].linear[0].i);
        *data++ = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);

        sum = pa_mult_s16_volume(*ptr0++, streams[0].linear[1].i);
        sum += pa_mult_s16_volume(*ptr1++, streams[1].linear[1].i);
        *data++ = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
    }
}

/* special case: mix 2 s16ne streams, 4 channels each */
static void pa_mix2_ch4_s16ne_neon(pa_mix_info streams[], int16_t *data, unsigned length) {
    const int16_t *ptr0 = streams[0].ptr;
    const int16_t *ptr1 = streams[1].ptr;

    int32x4_t sv0, sv1;

    __asm__ __volatile__ (
        "vld1.s32 %h[sv0], [%[lin0]]         \n\t"
        "vld1.s32 %h[sv1], [%[lin1]]         \n\t"
        : [sv0] "=w" (sv0), [sv1] "=w" (sv1)
        : [lin0] "r" (streams[0].linear), [lin1] "r" (streams[1].linear)
        : /* clobber list */
    );

    length /= sizeof(int16_t);
    for (; length >= 4; length -= 4) {
        __asm__ __volatile__ (
            "vld1.s16    d0, [%[ptr0]]!      \n\t"
            "vld1.s16    d2, [%[ptr1]]!      \n\t"
            "vshll.s16   q0, d0, #15         \n\t"
            "vshll.s16   q1, d2, #15         \n\t"
            "vqdmulh.s32 q0, q0, %q[sv0]     \n\t"
            "vqdmulh.s32 q1, q1, %q[sv1]     \n\t"
            "vqadd.s32   q0, q0, q1          \n\t"
            "vqmovn.s32  d0, q0              \n\t"
            "vst1.s16    d0, [%[data]]!      \n\t"
            : [ptr0] "+r" (ptr0), [ptr1] "+r" (ptr1), [data] "+r" (data)
            : [sv0] "w" (sv0), [sv1] "w" (sv1)
            : "memory", "cc", "q0", "q1" /* clobber list */
        );
    }
}

static void pa_mix_s16ne_neon(pa_mix_info streams[], unsigned nstreams, unsigned nchannels, void *data, unsigned length) {
    if (nstreams == 2 && nchannels == 2)
        pa_mix2_ch2_s16ne_neon(streams, data, length);
    else if (nstreams == 2 && nchannels == 4)
        pa_mix2_ch4_s16ne_neon(streams, data, length);
    else if (nstreams == 2 && nchannels == 1)
        pa_mix2_ch1_s16ne_neon(streams, data, length);
    else if (nchannels == 2)
        pa_mix_ch2_s16ne_neon(streams, nstreams, data, length);
    else
        fallback(streams, nstreams, nchannels, data, length);
}

void pa_mix_func_init_neon(pa_cpu_arm_flag_t flags) {
    pa_log_info("Initialising ARM NEON optimized mixing functions.");

    fallback = pa_get_mix_func(PA_SAMPLE_S16NE);
    pa_set_mix_func(PA_SAMPLE_S16NE, (pa_do_mix_func_t) pa_mix_s16ne_neon);
}
