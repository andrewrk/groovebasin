/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2009 Wim Taymans <wim.taymans@collabora.co.uk.com>

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <pulse/xmalloc.h>
#include <pulse/sample.h>
#include <pulse/volume.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "cpu.h"
#include "remap.h"

static void remap_mono_to_stereo_s16ne_c(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i; i--) {
        dst[0] = dst[1] = src[0];
        dst[2] = dst[3] = src[1];
        dst[4] = dst[5] = src[2];
        dst[6] = dst[7] = src[3];
        src += 4;
        dst += 8;
    }
    for (i = n & 3; i; i--) {
        dst[0] = dst[1] = src[0];
        src++;
        dst += 2;
    }
}

static void remap_mono_to_stereo_s32ne_c(pa_remap_t *m, int32_t *dst, const int32_t *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i; i--) {
        dst[0] = dst[1] = src[0];
        dst[2] = dst[3] = src[1];
        dst[4] = dst[5] = src[2];
        dst[6] = dst[7] = src[3];
        src += 4;
        dst += 8;
    }
    for (i = n & 3; i; i--) {
        dst[0] = dst[1] = src[0];
        src++;
        dst += 2;
    }
}

static void remap_mono_to_stereo_float32ne_c(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i; i--) {
        dst[0] = dst[1] = src[0];
        dst[2] = dst[3] = src[1];
        dst[4] = dst[5] = src[2];
        dst[6] = dst[7] = src[3];
        src += 4;
        dst += 8;
    }
    for (i = n & 3; i; i--) {
        dst[0] = dst[1] = src[0];
        src++;
        dst += 2;
    }
}

static void remap_stereo_to_mono_s16ne_c(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i > 0; i--) {
        dst[0] = (src[0] + src[1])/2;
        dst[1] = (src[2] + src[3])/2;
        dst[2] = (src[4] + src[5])/2;
        dst[3] = (src[6] + src[7])/2;
        src += 8;
        dst += 4;
    }
    for (i = n & 3; i; i--) {
        dst[0] = (src[0] + src[1])/2;
        src += 2;
        dst += 1;
    }
}

static void remap_stereo_to_mono_s32ne_c(pa_remap_t *m, int32_t *dst, const int32_t *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i > 0; i--) {
        /* Avoid overflow by performing division first. We accept a
         * difference of +/- 1 to the ideal result. */
        dst[0] = (src[0]/2 + src[1]/2);
        dst[1] = (src[2]/2 + src[3]/2);
        dst[2] = (src[4]/2 + src[5]/2);
        dst[3] = (src[6]/2 + src[7]/2);
        src += 8;
        dst += 4;
    }
    for (i = n & 3; i; i--) {
        /* Avoid overflow by performing division first. We accept a
         * difference of +/- 1 to the ideal result. */
        dst[0] = (src[0]/2 + src[1]/2);
        src += 2;
        dst += 1;
    }
}

static void remap_stereo_to_mono_float32ne_c(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i > 0; i--) {
        dst[0] = (src[0] + src[1])*0.5f;
        dst[1] = (src[2] + src[3])*0.5f;
        dst[2] = (src[4] + src[5])*0.5f;
        dst[3] = (src[6] + src[7])*0.5f;
        src += 8;
        dst += 4;
    }
    for (i = n & 3; i; i--) {
        dst[0] = (src[0] + src[1])*0.5f;
        src += 2;
        dst += 1;
    }
}

static void remap_mono_to_ch4_s16ne_c(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i; i--) {
        dst[0] = dst[1] = dst[2] = dst[3] = src[0];
        dst[4] = dst[5] = dst[6] = dst[7] = src[1];
        dst[8] = dst[9] = dst[10] = dst[11] = src[2];
        dst[12] = dst[13] = dst[14] = dst[15] = src[3];
        src += 4;
        dst += 16;
    }
    for (i = n & 3; i; i--) {
        dst[0] = dst[1] = dst[2] = dst[3] = src[0];
        src++;
        dst += 4;
    }
}

static void remap_mono_to_ch4_s32ne_c(pa_remap_t *m, int32_t *dst, const int32_t *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i; i--) {
        dst[0] = dst[1] = dst[2] = dst[3] = src[0];
        dst[4] = dst[5] = dst[6] = dst[7] = src[1];
        dst[8] = dst[9] = dst[10] = dst[11] = src[2];
        dst[12] = dst[13] = dst[14] = dst[15] = src[3];
        src += 4;
        dst += 16;
    }
    for (i = n & 3; i; i--) {
        dst[0] = dst[1] = dst[2] = dst[3] = src[0];
        src++;
        dst += 4;
    }
}

static void remap_mono_to_ch4_float32ne_c(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i; i--) {
        dst[0] = dst[1] = dst[2] = dst[3] = src[0];
        dst[4] = dst[5] = dst[6] = dst[7] = src[1];
        dst[8] = dst[9] = dst[10] = dst[11] = src[2];
        dst[12] = dst[13] = dst[14] = dst[15] = src[3];
        src += 4;
        dst += 16;
    }
    for (i = n & 3; i; i--) {
        dst[0] = dst[1] = dst[2] = dst[3] = src[0];
        src++;
        dst += 4;
    }
}

static void remap_ch4_to_mono_s16ne_c(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i > 0; i--) {
        dst[0] = (src[0] + src[1] + src[2] + src[3])/4;
        dst[1] = (src[4] + src[5] + src[6] + src[7])/4;
        dst[2] = (src[8] + src[9] + src[10] + src[11])/4;
        dst[3] = (src[12] + src[13] + src[14] + src[15])/4;
        src += 16;
        dst += 4;
    }
    for (i = n & 3; i; i--) {
        dst[0] = (src[0] + src[1] + src[2] + src[3])/4;
        src += 4;
        dst += 1;
    }
}

static void remap_ch4_to_mono_s32ne_c(pa_remap_t *m, int32_t *dst, const int32_t *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i > 0; i--) {
        /* Avoid overflow by performing division first. We accept a
         * difference of +/- 3 to the ideal result. */
        dst[0] = (src[0]/4 + src[1]/4 + src[2]/4 + src[3]/4);
        dst[1] = (src[4]/4 + src[5]/4 + src[6]/4 + src[7]/4);
        dst[2] = (src[8]/4 + src[9]/4 + src[10]/4 + src[11]/4);
        dst[3] = (src[12]/4 + src[13]/4 + src[14]/4 + src[15]/4);
        src += 16;
        dst += 4;
    }
    for (i = n & 3; i; i--) {
        /* Avoid overflow by performing division first. We accept a
         * difference of +/- 3 to the ideal result. */
        dst[0] = (src[0]/4 + src[1]/4 + src[2]/4 + src[3]/4);
        src += 4;
        dst += 1;
    }
}

static void remap_ch4_to_mono_float32ne_c(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    unsigned i;

    for (i = n >> 2; i > 0; i--) {
        dst[0] = (src[0] + src[1] + src[2] + src[3])*0.25f;
        dst[1] = (src[4] + src[5] + src[6] + src[7])*0.25f;
        dst[2] = (src[8] + src[9] + src[10] + src[11])*0.25f;
        dst[3] = (src[12] + src[13] + src[14] + src[15])*0.25f;
        src += 16;
        dst += 4;
    }
    for (i = n & 3; i; i--) {
        dst[0] = (src[0] + src[1] + src[2] + src[3])*0.25f;
        src += 4;
        dst += 1;
    }
}

static void remap_channels_matrix_s16ne_c(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {

    unsigned oc, ic, i;
    unsigned n_ic, n_oc;

    n_ic = m->i_ss.channels;
    n_oc = m->o_ss.channels;

    memset(dst, 0, n * sizeof(int16_t) * n_oc);

    for (oc = 0; oc < n_oc; oc++) {

        for (ic = 0; ic < n_ic; ic++) {
            int16_t *d = dst + oc;
            const int16_t *s = src + ic;
            int32_t vol = m->map_table_i[oc][ic];

            if (vol <= 0)
                continue;

            if (vol >= 0x10000) {
                for (i = n; i > 0; i--, s += n_ic, d += n_oc)
                    *d += *s;
            } else {
                for (i = n; i > 0; i--, s += n_ic, d += n_oc)
                    *d += (int16_t) (((int32_t)*s * vol) >> 16);
            }
        }
    }
}

static void remap_channels_matrix_s32ne_c(pa_remap_t *m, int32_t *dst, const int32_t *src, unsigned n) {
    unsigned oc, ic, i;
    unsigned n_ic, n_oc;

    n_ic = m->i_ss.channels;
    n_oc = m->o_ss.channels;

    memset(dst, 0, n * sizeof(int32_t) * n_oc);

    for (oc = 0; oc < n_oc; oc++) {

        for (ic = 0; ic < n_ic; ic++) {
            int32_t *d = dst + oc;
            const int32_t *s = src + ic;
            int32_t vol = m->map_table_i[oc][ic];

            if (vol <= 0)
                continue;

            if (vol >= 0x10000) {
                for (i = n; i > 0; i--, s += n_ic, d += n_oc)
                    *d += *s;
            } else {
                for (i = n; i > 0; i--, s += n_ic, d += n_oc)
                    *d += (int32_t) (((int64_t)*s * vol) >> 16);
            }
        }
    }
}

static void remap_channels_matrix_float32ne_c(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    unsigned oc, ic, i;
    unsigned n_ic, n_oc;

    n_ic = m->i_ss.channels;
    n_oc = m->o_ss.channels;

    memset(dst, 0, n * sizeof(float) * n_oc);

    for (oc = 0; oc < n_oc; oc++) {

        for (ic = 0; ic < n_ic; ic++) {
            float *d = dst + oc;
            const float *s = src + ic;
            float vol = m->map_table_f[oc][ic];

            if (vol <= 0.0f)
                continue;

            if (vol >= 1.0f) {
                for (i = n; i > 0; i--, s += n_ic, d += n_oc)
                    *d += *s;
            } else {
                for (i = n; i > 0; i--, s += n_ic, d += n_oc)
                    *d += *s * vol;
            }
        }
    }
}

/* Produce an array containing input channel indices to map to output channels.
 * If the output channel is empty, the array element is -1. */
bool pa_setup_remap_arrange(const pa_remap_t *m, int8_t arrange[PA_CHANNELS_MAX]) {
    unsigned ic, oc;
    unsigned n_ic, n_oc;
    unsigned count_output = 0;

    pa_assert(m);

    n_ic = m->i_ss.channels;
    n_oc = m->o_ss.channels;

    for (oc = 0; oc < n_oc; oc++) {
        arrange[oc] = -1;
        for (ic = 0; ic < n_ic; ic++) {
            int32_t vol = m->map_table_i[oc][ic];

            /* input channel is not used */
            if (vol == 0)
                continue;

            /* if mixing this channel, we cannot just rearrange */
            if (vol != 0x10000 || arrange[oc] >= 0)
                return false;

            arrange[oc] = ic;
            count_output++;
        }
    }

    return count_output > 0;
}

static void remap_arrange_mono_s16ne_c(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    const unsigned n_ic = m->i_ss.channels;
    const int8_t *arrange = m->state;

    src += arrange[0];
    for (; n > 0; n--) {
        *dst++ = *src;
        src += n_ic;
    }
}

static void remap_arrange_stereo_s16ne_c(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    const unsigned n_ic = m->i_ss.channels;
    const int8_t *arrange = m->state;
    const int8_t ic0 = arrange[0], ic1 = arrange[1];

    for (; n > 0; n--) {
        *dst++ = (ic0 >= 0) ? *(src + ic0) : 0;
        *dst++ = (ic1 >= 0) ? *(src + ic1) : 0;
        src += n_ic;
    }
}

static void remap_arrange_ch4_s16ne_c(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    const unsigned n_ic = m->i_ss.channels;
    const int8_t *arrange = m->state;
    const int8_t ic0 = arrange[0], ic1 = arrange[1],
        ic2 = arrange[2], ic3 = arrange[3];

    for (; n > 0; n--) {
        *dst++ = (ic0 >= 0) ? *(src + ic0) : 0;
        *dst++ = (ic1 >= 0) ? *(src + ic1) : 0;
        *dst++ = (ic2 >= 0) ? *(src + ic2) : 0;
        *dst++ = (ic3 >= 0) ? *(src + ic3) : 0;
        src += n_ic;
    }
}

static void remap_arrange_mono_s32ne_c(pa_remap_t *m, int32_t *dst, const int32_t *src, unsigned n) {
    const unsigned n_ic = m->i_ss.channels;
    const int8_t *arrange = m->state;

    src += arrange[0];
    for (; n > 0; n--) {
        *dst++ = *src;
        src += n_ic;
    }
}

static void remap_arrange_stereo_s32ne_c(pa_remap_t *m, int32_t *dst, const int32_t *src, unsigned n) {
    const unsigned n_ic = m->i_ss.channels;
    const int8_t *arrange = m->state;
    const int ic0 = arrange[0], ic1 = arrange[1];

    for (; n > 0; n--) {
        *dst++ = (ic0 >= 0) ? *(src + ic0) : 0;
        *dst++ = (ic1 >= 0) ? *(src + ic1) : 0;
        src += n_ic;
    }
}

static void remap_arrange_ch4_s32ne_c(pa_remap_t *m, int32_t *dst, const int32_t *src, unsigned n) {
    const unsigned n_ic = m->i_ss.channels;
    const int8_t *arrange = m->state;
    const int ic0 = arrange[0], ic1 = arrange[1],
        ic2 = arrange[2], ic3 = arrange[3];

    for (; n > 0; n--) {
        *dst++ = (ic0 >= 0) ? *(src + ic0) : 0;
        *dst++ = (ic1 >= 0) ? *(src + ic1) : 0;
        *dst++ = (ic2 >= 0) ? *(src + ic2) : 0;
        *dst++ = (ic3 >= 0) ? *(src + ic3) : 0;
        src += n_ic;
    }
}

static void remap_arrange_mono_float32ne_c(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    const unsigned n_ic = m->i_ss.channels;
    const int8_t *arrange = m->state;

    src += arrange[0];
    for (; n > 0; n--) {
        *dst++ = *src;
        src += n_ic;
    }
}

static void remap_arrange_stereo_float32ne_c(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    const unsigned n_ic = m->i_ss.channels;
    const int8_t *arrange = m->state;
    const int ic0 = arrange[0], ic1 = arrange[1];

    for (; n > 0; n--) {
        *dst++ = (ic0 >= 0) ? *(src + ic0) : 0.0f;
        *dst++ = (ic1 >= 0) ? *(src + ic1) : 0.0f;
        src += n_ic;
    }
}

static void remap_arrange_ch4_float32ne_c(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    const unsigned n_ic = m->i_ss.channels;
    const int8_t *arrange = m->state;
    const int ic0 = arrange[0], ic1 = arrange[1],
        ic2 = arrange[2], ic3 = arrange[3];

    for (; n > 0; n--) {
        *dst++ = (ic0 >= 0) ? *(src + ic0) : 0.0f;
        *dst++ = (ic1 >= 0) ? *(src + ic1) : 0.0f;
        *dst++ = (ic2 >= 0) ? *(src + ic2) : 0.0f;
        *dst++ = (ic3 >= 0) ? *(src + ic3) : 0.0f;
        src += n_ic;
    }
}

void pa_set_remap_func(pa_remap_t *m, pa_do_remap_func_t func_s16,
    pa_do_remap_func_t func_s32, pa_do_remap_func_t func_float) {

    pa_assert(m);

    if (m->format == PA_SAMPLE_S16NE)
        m->do_remap = func_s16;
    else if (m->format == PA_SAMPLE_S32NE)
        m->do_remap = func_s32;
    else if (m->format == PA_SAMPLE_FLOAT32NE)
        m->do_remap = func_float;
    else
        pa_assert_not_reached();
    pa_assert(m->do_remap);
}

static bool force_generic_code = false;

/* set the function that will execute the remapping based on the matrices */
static void init_remap_c(pa_remap_t *m) {
    unsigned n_oc, n_ic;
    int8_t arrange[PA_CHANNELS_MAX];

    n_oc = m->o_ss.channels;
    n_ic = m->i_ss.channels;

    /* find some common channel remappings, fall back to full matrix operation. */
    if (force_generic_code) {
        pa_log_info("Forced to use generic matrix remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_channels_matrix_s16ne_c,
            (pa_do_remap_func_t) remap_channels_matrix_s32ne_c,
            (pa_do_remap_func_t) remap_channels_matrix_float32ne_c);
        return;
    }

    if (n_ic == 1 && n_oc == 2 &&
            m->map_table_i[0][0] == 0x10000 && m->map_table_i[1][0] == 0x10000) {

        pa_log_info("Using mono to stereo remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_mono_to_stereo_s16ne_c,
            (pa_do_remap_func_t) remap_mono_to_stereo_s32ne_c,
            (pa_do_remap_func_t) remap_mono_to_stereo_float32ne_c);
    } else if (n_ic == 2 && n_oc == 1 &&
            m->map_table_i[0][0] == 0x8000 && m->map_table_i[0][1] == 0x8000) {

        pa_log_info("Using stereo to mono remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_stereo_to_mono_s16ne_c,
            (pa_do_remap_func_t) remap_stereo_to_mono_s32ne_c,
            (pa_do_remap_func_t) remap_stereo_to_mono_float32ne_c);
    } else if (n_ic == 1 && n_oc == 4 &&
            m->map_table_i[0][0] == 0x10000 && m->map_table_i[1][0] == 0x10000 &&
            m->map_table_i[2][0] == 0x10000 && m->map_table_i[3][0] == 0x10000) {

        pa_log_info("Using mono to 4-channel remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t)remap_mono_to_ch4_s16ne_c,
            (pa_do_remap_func_t) remap_mono_to_ch4_s32ne_c,
            (pa_do_remap_func_t) remap_mono_to_ch4_float32ne_c);
    } else if (n_ic == 4 && n_oc == 1 &&
            m->map_table_i[0][0] == 0x4000 && m->map_table_i[0][1] == 0x4000 &&
            m->map_table_i[0][2] == 0x4000 && m->map_table_i[0][3] == 0x4000) {

        pa_log_info("Using 4-channel to mono remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_ch4_to_mono_s16ne_c,
            (pa_do_remap_func_t) remap_ch4_to_mono_s32ne_c,
            (pa_do_remap_func_t) remap_ch4_to_mono_float32ne_c);
    } else if (pa_setup_remap_arrange(m, arrange) && n_oc == 1) {

        pa_log_info("Using mono arrange remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_arrange_mono_s16ne_c,
            (pa_do_remap_func_t) remap_arrange_mono_s32ne_c,
            (pa_do_remap_func_t) remap_arrange_mono_float32ne_c);

        /* setup state */
        m->state = pa_xnewdup(int8_t, arrange, PA_CHANNELS_MAX);
    } else if (pa_setup_remap_arrange(m, arrange) && n_oc == 2) {

        pa_log_info("Using stereo arrange remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_arrange_stereo_s16ne_c,
            (pa_do_remap_func_t) remap_arrange_stereo_s32ne_c,
            (pa_do_remap_func_t) remap_arrange_stereo_float32ne_c);

        /* setup state */
        m->state = pa_xnewdup(int8_t, arrange, PA_CHANNELS_MAX);
    } else if (pa_setup_remap_arrange(m, arrange) && n_oc == 4) {

        pa_log_info("Using 4-channel arrange remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_arrange_ch4_s16ne_c,
            (pa_do_remap_func_t) remap_arrange_ch4_s32ne_c,
            (pa_do_remap_func_t) remap_arrange_ch4_float32ne_c);

        /* setup state */
        m->state = pa_xnewdup(int8_t, arrange, PA_CHANNELS_MAX);
    } else {

        pa_log_info("Using generic matrix remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_channels_matrix_s16ne_c,
            (pa_do_remap_func_t) remap_channels_matrix_s32ne_c,
            (pa_do_remap_func_t) remap_channels_matrix_float32ne_c);
    }
}

/* default C implementation */
static pa_init_remap_func_t init_remap_func = init_remap_c;

void pa_init_remap_func(pa_remap_t *m) {
    pa_assert(init_remap_func);

    m->do_remap = NULL;

    /* call the installed remap init function */
    init_remap_func(m);

    if (m->do_remap == NULL) {
        /* nothing was installed, fallback to C version */
        init_remap_c(m);
    }
}

pa_init_remap_func_t pa_get_init_remap_func(void) {
    return init_remap_func;
}

void pa_set_init_remap_func(pa_init_remap_func_t func) {
    init_remap_func = func;
}

void pa_remap_func_init(const pa_cpu_info *cpu_info) {
    force_generic_code = cpu_info->force_generic_code;
}
