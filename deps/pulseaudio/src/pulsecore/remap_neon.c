/***
  This file is part of PulseAudio.

  Copyright 2013 Peter Meerwald <p.meerwald@bct-electronic.com>

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

#include <pulse/sample.h>
#include <pulse/xmalloc.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "cpu-arm.h"
#include "remap.h"

#include <arm_neon.h>

static void remap_mono_to_stereo_float32ne_neon_a8(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    for (; n >= 4; n -= 4) {
        __asm__ __volatile__ (
            "vld1.32    {q0}, [%[src]]!         \n\t"
            "vmov       q1, q0                  \n\t"
            "vst2.32    {q0,q1}, [%[dst]]!      \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : /* input operands */
            : "memory", "q0", "q1" /* clobber list */
        );
    }

    for (; n > 0; n--) {
        dst[0] = dst[1] = src[0];
        src++;
        dst += 2;
    }
}

static void remap_mono_to_stereo_float32ne_generic_arm(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    for (; n >= 2; n -= 2) {
        __asm__ __volatile__ (
            "ldm        %[src]!, {r4,r6}        \n\t"
            "mov        r5, r4                  \n\t"

            /* We use r12 instead of r7 here, because r7 is reserved for the
             * frame pointer when using Thumb. */
            "mov        r12, r6                 \n\t"

            "stm        %[dst]!, {r4-r6,r12}    \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : /* input operands */
            : "memory", "r4", "r5", "r6", "r12" /* clobber list */
        );
    }

    if (n > 0)
        dst[0] = dst[1] = src[0];
}

static void remap_mono_to_stereo_s16ne_neon(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    for (; n >= 8; n -= 8) {
        __asm__ __volatile__ (
            "vld1.16    {q0}, [%[src]]!         \n\t"
            "vmov       q1, q0                  \n\t"
            "vst2.16    {q0,q1}, [%[dst]]!      \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : /* input operands */
            : "memory", "q0", "q1" /* clobber list */
        );
    }

    for (; n > 0; n--) {
        dst[0] = dst[1] = src[0];
        src++;
        dst += 2;
    }
}

static void remap_mono_to_ch4_float32ne_neon(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    for (; n >= 2; n -= 2) {
        __asm__ __volatile__ (
            "vld1.32    {d0}, [%[src]]!         \n\t"
            "vdup.f32   q1, d0[0]               \n\t"
            "vdup.f32   q2, d0[1]               \n\t"
            "vst1.32    {q1,q2}, [%[dst]]!      \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : /* input operands */
            : "memory", "q0", "q1", "q2" /* clobber list */
        );
    }

    if (n--)
        dst[0] = dst[1] = dst[2] = dst[3] = src[0];
}

static void remap_mono_to_ch4_s16ne_neon(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    for (; n >= 4; n -= 4) {
        __asm__ __volatile__ (
            "vld1.16    {d0}, [%[src]]!         \n\t"
            "vdup.s16   d1, d0[1]               \n\t"
            "vdup.s16   d2, d0[2]               \n\t"
            "vdup.s16   d3, d0[3]               \n\t"
            "vdup.s16   d0, d0[0]               \n\t"
            "vst1.16    {d0,d1,d2,d3}, [%[dst]]!\n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : /* input operands */
            : "memory", "d0", "d1", "d2", "d3" /* clobber list */
        );
    }

    for (; n > 0; n--) {
        dst[0] = dst[1] = dst[2] = dst[3] = src[0];
        src++;
        dst += 4;
    }
}

static void remap_stereo_to_mono_float32ne_neon(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    const float32x4_t halve = vdupq_n_f32(0.5f);
    for (; n >= 4; n -= 4) {
        __asm__ __volatile__ (
            "vld2.32    {q0,q1}, [%[src]]!      \n\t"
            "vadd.f32   q0, q0, q1              \n\t"
            "vmul.f32   q0, q0, %q[halve]       \n\t"
            "vst1.32    {q0}, [%[dst]]!         \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : [halve] "w" (halve) /* input operands */
            : "memory", "q0", "q1" /* clobber list */
        );
    }

    for (; n > 0; n--) {
        dst[0] = (src[0] + src[1])*0.5f;
        src += 2;
        dst++;
    }
}

static void remap_stereo_to_mono_s32ne_neon(pa_remap_t *m, int32_t *dst, const int32_t *src, unsigned n) {
    for (; n >= 4; n -= 4) {
        __asm__ __volatile__ (
            "vld2.32    {q0,q1}, [%[src]]!      \n\t"
            "vrhadd.s32 q0, q0, q1              \n\t"
            "vst1.32    {q0}, [%[dst]]!         \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : /* input operands */
            : "memory", "q0", "q1" /* clobber list */
        );
    }

    for (; n > 0; n--) {
        dst[0] = src[0]/2 + src[1]/2;
        src += 2;
        dst++;
    }
}

static void remap_stereo_to_mono_s16ne_neon(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    for (; n >= 8; n -= 8) {
        __asm__ __volatile__ (
            "vld2.16    {q0,q1}, [%[src]]!      \n\t"
            "vrhadd.s16 q0, q0, q1              \n\t"
            "vst1.16    {q0}, [%[dst]]!         \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : /* input operands */
            : "memory", "q0", "q1" /* clobber list */
        );
    }

    for (; n > 0; n--) {
        dst[0] = (src[0] + src[1])/2;
        src += 2;
        dst++;
    }
}

static void remap_ch4_to_mono_float32ne_neon(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    const float32x2_t quart = vdup_n_f32(0.25f);
    for (; n >= 2; n -= 2) {
        __asm__ __volatile__ (
            "vld4.32    {d0,d1,d2,d3}, [%[src]]!\n\t"
            "vadd.f32   d0, d0, d1              \n\t"
            "vadd.f32   d2, d2, d3              \n\t"
            "vadd.f32   d0, d0, d2              \n\t"
            "vmul.f32   d0, d0, %P[quart]       \n\t"
            "vst1.32    {d0}, [%[dst]]!         \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : [quart] "w" (quart) /* input operands */
            : "memory", "d0", "d1", "d2", "d3" /* clobber list */
        );
    }

    if (n > 0)
        dst[0] = (src[0] + src[1] + src[2] + src[3])*0.25f;
}

static void remap_ch4_to_mono_s16ne_neon(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    for (; n >= 4; n -= 4) {
        __asm__ __volatile__ (
            "vld4.16    {d0,d1,d2,d3}, [%[src]]!\n\t"
            "vrhadd.s16 d0, d0, d1              \n\t"
            "vrhadd.s16 d2, d2, d3              \n\t"
            "vrhadd.s16 d0, d0, d2              \n\t"
            "vst1.16    {d0}, [%[dst]]!         \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : /* input operands */
            : "memory", "d0", "d1", "d2", "d3" /* clobber list */
        );
    }

    for (; n > 0; n--) {
        dst[0] = (src[0] + src[1] + src[2] + src[3])/4;
        src += 4;
        dst++;
    }
}

static void remap_ch4_s16ne_neon(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    int32x4_t *f = m->state;
    const int32x4_t f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3];

    for (; n > 0; n--) {
        __asm__ __volatile__ (
            "vld1.16    {d0}, [%[src]]!         \n\t"
            "vmovl.s16  q0, d0                  \n\t"
            "vdup.s32   q1, d0[0]               \n\t"
            "vmul.s32   q1, q1, %q[f0]          \n\t"
            "vdup.s32   q2, d0[1]               \n\t"
            "vmla.s32   q1, q2, %q[f1]          \n\t"
            "vdup.s32   q2, d1[0]               \n\t"
            "vmla.s32   q1, q2, %q[f2]          \n\t"
            "vdup.s32   q2, d1[1]               \n\t"
            "vmla.s32   q1, q2, %q[f3]          \n\t"
            "vqshrn.s32  d2, q1, #16            \n\t"
            "vst1.32    {d2}, [%[dst]]!         \n\t"
            : [dst] "+r" (dst), [src] "+r" (src)
            : [f0] "w" (f0), [f1] "w" (f1), [f2] "w" (f2), [f3] "w" (f3)
            : "memory", "q0", "q1", "q2"
        );
    }
}

static void remap_ch4_float32ne_neon(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    float32x4_t *f = m->state;
    const float32x4_t f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3];

    for (; n > 0; n--) {
        __asm__ __volatile__ (
            "vld1.32    {d0,d1}, [%[src]]!      \n\t"
            "vdup.f32   q1, d0[0]               \n\t"
            "vmul.f32   q1, q1, %q[f0]          \n\t"
            "vdup.f32   q2, d0[1]               \n\t"
            "vmla.f32   q1, q2, %q[f1]          \n\t"
            "vdup.f32   q2, d1[0]               \n\t"
            "vmla.f32   q1, q2, %q[f2]          \n\t"
            "vdup.f32   q2, d1[1]               \n\t"
            "vmla.f32   q1, q2, %q[f3]          \n\t"
            "vst1.32    {d2,d3}, [%[dst]]!      \n\t"
            : [dst] "+r" (dst), [src] "+r" (src)
            : [f0] "w" (f0), [f1] "w" (f1), [f2] "w" (f2), [f3] "w" (f3)
            : "memory", "q0", "q1", "q2"
        );
    }
}

static void remap_arrange_stereo_s16ne_neon(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    const uint8x8_t t = ((uint8x8_t *) m->state)[0];

    for (; n >= 2; n -= 2) {
        __asm__ __volatile__ (
            "vld1.s16   d0, [%[src]]!           \n\t"
            "vtbl.8     d0, {d0}, %P[t]         \n\t"
            "vst1.s16   d0, [%[dst]]!           \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : [t] "w" (t) /* input operands */
            : "memory", "d0" /* clobber list */
        );
    }

    if (n > 0) {
        __asm__ __volatile__ (
            "vld1.32   d0[0], [%[src]]!         \n\t"
            "vtbl.8    d0, {d0}, %P[t]          \n\t"
            "vst1.32   d0[0], [%[dst]]!         \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : [t] "w" (t) /* input operands */
            : "memory", "d0" /* clobber list */
        );
    }
}

static void remap_arrange_ch2_ch4_s16ne_neon(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    const uint8x8_t t = ((uint8x8_t *) m->state)[0];

    for (; n > 0; n--) {
        __asm__ __volatile__ (
            "vld1.32    d0[0], [%[src]]!           \n\t"
            "vtbl.8     d0, {d0}, %P[t]            \n\t"
            "vst1.s16   d0, [%[dst]]!              \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : [t] "w" (t) /* input operands */
            : "memory", "d0" /* clobber list */
        );
    }
}

static void remap_arrange_ch4_s16ne_neon(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    const uint8x8_t t = ((uint8x8_t *) m->state)[0];

    for (; n > 0; n--) {
        __asm__ __volatile__ (
            "vld1.s16   d0, [%[src]]!           \n\t"
            "vtbl.8     d0, {d0}, %P[t]         \n\t"
            "vst1.s16   d0, [%[dst]]!           \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : [t] "w" (t) /* input operands */
            : "memory", "d0" /* clobber list */
        );
    }
}

static void remap_arrange_stereo_float32ne_neon(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    const uint8x8_t t = ((uint8x8_t *)m->state)[0];

    for (; n > 0; n--) {
        __asm__ __volatile__ (
            "vld1.f32   d0, [%[src]]!           \n\t"
            "vtbl.8     d0, {d0}, %P[t]         \n\t"
            "vst1.s16   {d0}, [%[dst]]!         \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : [t] "w" (t) /* input operands */
            : "memory", "d0" /* clobber list */
        );
    }
}

/* Works for both S32NE and FLOAT32NE */
static void remap_arrange_ch2_ch4_any32ne_neon(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    const uint8x8_t t0 = ((uint8x8_t *)m->state)[0];
    const uint8x8_t t1 = ((uint8x8_t *)m->state)[1];

    for (; n > 0; n--) {
        __asm__ __volatile__ (
            "vld1.f32   d0, [%[src]]!           \n\t"
            "vtbl.8     d1, {d0}, %P[t0]        \n\t"
            "vtbl.8     d2, {d0}, %P[t1]        \n\t"
            "vst1.s16   {d1,d2}, [%[dst]]!      \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : [t0] "w" (t0), [t1] "w" (t1) /* input operands */
            : "memory", "d0", "d1", "d2" /* clobber list */
        );
    }
}

static void remap_arrange_ch4_float32ne_neon(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    const uint8x8_t t0 = ((uint8x8_t *)m->state)[0];
    const uint8x8_t t1 = ((uint8x8_t *)m->state)[1];

    for (; n > 0; n--) {
        __asm__ __volatile__ (
            "vld1.f32   {d0,d1}, [%[src]]!      \n\t"
            "vtbl.8     d2, {d0,d1}, %P[t0]     \n\t"
            "vtbl.8     d3, {d0,d1}, %P[t1]     \n\t"
            "vst1.s16   {d2,d3}, [%[dst]]!      \n\t"
            : [dst] "+r" (dst), [src] "+r" (src) /* output operands */
            : [t0] "w" (t0), [t1] "w" (t1) /* input operands */
            : "memory", "d0", "d1", "d2", "d3" /* clobber list */
        );
    }
}

static pa_cpu_arm_flag_t arm_flags;

static void init_remap_neon(pa_remap_t *m) {
    unsigned n_oc, n_ic;
    int8_t arrange[PA_CHANNELS_MAX];

    n_oc = m->o_ss.channels;
    n_ic = m->i_ss.channels;

    /* We short-circuit remap function selection for S32NE in most
     * cases as the corresponding generic C code is performing
     * similarly or even better. However there are a few cases where
     * there actually is a significant improvement from using
     * hand-crafted NEON assembly so we cannot just bail out for S32NE
     * here. */
    if (n_ic == 1 && n_oc == 2 &&
            m->map_table_i[0][0] == 0x10000 && m->map_table_i[1][0] == 0x10000) {
        if (m->format == PA_SAMPLE_S32NE)
            return;
        if (arm_flags & PA_CPU_ARM_CORTEX_A8) {

            pa_log_info("Using ARM NEON/A8 mono to stereo remapping");
            pa_set_remap_func(m, (pa_do_remap_func_t) remap_mono_to_stereo_s16ne_neon,
                NULL, (pa_do_remap_func_t) remap_mono_to_stereo_float32ne_neon_a8);
        }
        else {
            pa_log_info("Using ARM NEON mono to stereo remapping");
            pa_set_remap_func(m, (pa_do_remap_func_t) remap_mono_to_stereo_s16ne_neon,
                NULL, (pa_do_remap_func_t) remap_mono_to_stereo_float32ne_generic_arm);
        }
    } else if (n_ic == 1 && n_oc == 4 &&
            m->map_table_i[0][0] == 0x10000 && m->map_table_i[1][0] == 0x10000 &&
            m->map_table_i[2][0] == 0x10000 && m->map_table_i[3][0] == 0x10000) {

        if (m->format == PA_SAMPLE_S32NE)
            return;
        pa_log_info("Using ARM NEON mono to 4-channel remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_mono_to_ch4_s16ne_neon,
            NULL, (pa_do_remap_func_t) remap_mono_to_ch4_float32ne_neon);
    } else if (n_ic == 2 && n_oc == 1 &&
            m->map_table_i[0][0] == 0x8000 && m->map_table_i[0][1] == 0x8000) {

        pa_log_info("Using ARM NEON stereo to mono remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_stereo_to_mono_s16ne_neon,
            (pa_do_remap_func_t) remap_stereo_to_mono_s32ne_neon,
            (pa_do_remap_func_t) remap_stereo_to_mono_float32ne_neon);
    } else if (n_ic == 4 && n_oc == 1 &&
            m->map_table_i[0][0] == 0x4000 && m->map_table_i[0][1] == 0x4000 &&
            m->map_table_i[0][2] == 0x4000 && m->map_table_i[0][3] == 0x4000) {

        if (m->format == PA_SAMPLE_S32NE)
            return;
        pa_log_info("Using ARM NEON 4-channel to mono remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_ch4_to_mono_s16ne_neon,
            NULL, (pa_do_remap_func_t) remap_ch4_to_mono_float32ne_neon);
    } else if (pa_setup_remap_arrange(m, arrange) &&
        ((n_ic == 2 && n_oc == 2) ||
         (n_ic == 2 && n_oc == 4) ||
         (n_ic == 4 && n_oc == 4))) {
        unsigned o;

        if (n_ic == 2 && n_oc == 2) {
            if (m->format == PA_SAMPLE_S32NE)
                return;
            pa_log_info("Using NEON stereo arrange remapping");
            pa_set_remap_func(m, (pa_do_remap_func_t) remap_arrange_stereo_s16ne_neon,
                NULL, (pa_do_remap_func_t) remap_arrange_stereo_float32ne_neon);
        } else if (n_ic == 2 && n_oc == 4) {
            pa_log_info("Using NEON 2-channel to 4-channel arrange remapping");
            pa_set_remap_func(m, (pa_do_remap_func_t) remap_arrange_ch2_ch4_s16ne_neon,
                (pa_do_remap_func_t) remap_arrange_ch2_ch4_any32ne_neon,
                (pa_do_remap_func_t) remap_arrange_ch2_ch4_any32ne_neon);
        } else if (n_ic == 4 && n_oc == 4) {
            if (m->format == PA_SAMPLE_S32NE)
                return;
            pa_log_info("Using NEON 4-channel arrange remapping");
            pa_set_remap_func(m, (pa_do_remap_func_t) remap_arrange_ch4_s16ne_neon,
                NULL, (pa_do_remap_func_t) remap_arrange_ch4_float32ne_neon);
        }

        /* setup state */
        switch (m->format) {
        case PA_SAMPLE_S16NE: {
            uint8x8_t *t = m->state = pa_xnew0(uint8x8_t, 1);
            for (o = 0; o < 4; o++) {
                if (arrange[o % n_oc] >= 0) {
                    /* convert channel index to vtbl indices */
                    unsigned frame = o / n_oc;
                    ((uint8_t *) t)[o * 2 + 0] = (frame * n_oc + arrange[o % n_oc]) * 2 + 0;
                    ((uint8_t *) t)[o * 2 + 1] = (frame * n_oc + arrange[o % n_oc]) * 2 + 1;
                } else {
                    /* use invalid table indices to map to 0 */
                    ((uint8_t *) t)[o * 2 + 0] = 0xff;
                    ((uint8_t *) t)[o * 2 + 1] = 0xff;
                }
            }
            break;
        }
        case PA_SAMPLE_S32NE:
                /* fall-through */
        case PA_SAMPLE_FLOAT32NE: {
            uint8x8_t *t = m->state = pa_xnew0(uint8x8_t, 2);
            for (o = 0; o < n_oc; o++) {
                if (arrange[o] >= 0) {
                    /* convert channel index to vtbl indices */
                    ((uint8_t *) t)[o * 4 + 0] = arrange[o] * 4 + 0;
                    ((uint8_t *) t)[o * 4 + 1] = arrange[o] * 4 + 1;
                    ((uint8_t *) t)[o * 4 + 2] = arrange[o] * 4 + 2;
                    ((uint8_t *) t)[o * 4 + 3] = arrange[o] * 4 + 3;
                } else {
                    /* use invalid table indices to map to 0 */
                    ((uint8_t *) t)[o * 4 + 0] = 0xff;
                    ((uint8_t *) t)[o * 4 + 1] = 0xff;
                    ((uint8_t *) t)[o * 4 + 2] = 0xff;
                    ((uint8_t *) t)[o * 4 + 3] = 0xff;
                }
            }
            break;
        }
        default:
            pa_assert_not_reached();
        }
    } else if (n_ic == 4 && n_oc == 4) {
        unsigned i, o;

        if (m->format == PA_SAMPLE_S32NE)
            return;
        pa_log_info("Using ARM NEON 4-channel remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_ch4_s16ne_neon,
            (pa_do_remap_func_t) NULL,
            (pa_do_remap_func_t) remap_ch4_float32ne_neon);

        /* setup state */
        switch (m->format) {
        case PA_SAMPLE_S16NE: {
            int32x4_t *f = m->state = pa_xnew0(int32x4_t, 4);
            for (o = 0; o < 4; o++) {
                for (i = 0; i < 4; i++) {
                    ((int *) &f[i])[o] = PA_CLAMP_UNLIKELY(m->map_table_i[o][i], 0, 0x10000);
                }
            }
            break;
        }
        case PA_SAMPLE_FLOAT32NE: {
            float32x4_t *f = m->state = pa_xnew0(float32x4_t, 4);
            for (o = 0; o < 4; o++) {
                for (i = 0; i < 4; i++) {
                    ((float *) &f[i])[o] = PA_CLAMP_UNLIKELY(m->map_table_f[o][i], 0.0f, 1.0f);
                }
            }
            break;
        }
        default:
            pa_assert_not_reached();
        }
    }
}

void pa_remap_func_init_neon(pa_cpu_arm_flag_t flags) {
    pa_log_info("Initialising ARM NEON optimized remappers.");
    arm_flags = flags;
    pa_set_init_remap_func((pa_init_remap_func_t) init_remap_neon);
}
