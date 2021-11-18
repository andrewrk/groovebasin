/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <pulsecore/g711.h>
#include <pulsecore/macro.h>
#include <pulsecore/endianmacros.h>

#include <pulsecore/sconv-s16le.h>
#include <pulsecore/sconv-s16be.h>

#include "sconv.h"

/* u8 */
static void u8_to_float32ne(unsigned n, const uint8_t *a, float *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *b = (*a * 1.0/128.0) - 1.0;
}

static void u8_from_float32ne(unsigned n, const float *a, uint8_t *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++) {
        float v;
        v = (*a * 127.0) + 128.0;
        v = PA_CLAMP_UNLIKELY (v, 0.0, 255.0);
        *b = rint (v);
    }
}

static void u8_to_s16ne(unsigned n, const uint8_t *a, int16_t *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *b = (((int16_t)*a) - 128) << 8;
}

static void u8_from_s16ne(unsigned n, const int16_t *a, uint8_t *b) {

    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *b = (uint8_t) ((uint16_t) *a >> 8) + (uint8_t) 0x80U;
}

/* float32 */

static void float32ne_to_float32ne(unsigned n, const float *a, float *b) {
    pa_assert(a);
    pa_assert(b);

    memcpy(b, a, (int) (sizeof(float) * n));
}

static void float32re_to_float32ne(unsigned n, const float *a, float *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *((uint32_t *) b) = PA_UINT32_SWAP(*((uint32_t *) a));
}

/* s16 */

static void s16ne_to_s16ne(unsigned n, const int16_t *a, int16_t *b) {
    pa_assert(a);
    pa_assert(b);

    memcpy(b, a, (int) (sizeof(int16_t) * n));
}

static void s16re_to_s16ne(unsigned n, const int16_t *a, int16_t *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *b = PA_INT16_SWAP(*a);
}

/* ulaw */

static void ulaw_to_float32ne(unsigned n, const uint8_t *a, float *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--)
        *(b++) = (float) st_ulaw2linear16(*(a++)) / 0x8000;
}

static void ulaw_from_float32ne(unsigned n, const float *a, uint8_t *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--) {
        float v = *(a++);
        v = PA_CLAMP_UNLIKELY(v, -1.0f, 1.0f);
        v *= 0x1FFF;
        *(b++) = st_14linear2ulaw((int16_t) lrintf(v));
    }
}

static void ulaw_to_s16ne(unsigned n, const uint8_t *a, int16_t *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *b = st_ulaw2linear16(*a);
}

static void ulaw_from_s16ne(unsigned n, const int16_t *a, uint8_t *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *b = st_14linear2ulaw(*a >> 2);
}

/* alaw */

static void alaw_to_float32ne(unsigned n, const uint8_t *a, float *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *b = (float) st_alaw2linear16(*a) / 0x8000;
}

static void alaw_from_float32ne(unsigned n, const float *a, uint8_t *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++) {
        float v = *a;
        v = PA_CLAMP_UNLIKELY(v, -1.0f, 1.0f);
        v *= 0xFFF;
        *b = st_13linear2alaw((int16_t) lrintf(v));
    }
}

static void alaw_to_s16ne(unsigned n, const int8_t *a, int16_t *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *b = st_alaw2linear16((uint8_t) *a);
}

static void alaw_from_s16ne(unsigned n, const int16_t *a, uint8_t *b) {
    pa_assert(a);
    pa_assert(b);

    for (; n > 0; n--, a++, b++)
        *b = st_13linear2alaw(*a >> 3);
}

static pa_convert_func_t to_float32ne_table[] = {
    [PA_SAMPLE_U8]        = (pa_convert_func_t) u8_to_float32ne,
    [PA_SAMPLE_ALAW]      = (pa_convert_func_t) alaw_to_float32ne,
    [PA_SAMPLE_ULAW]      = (pa_convert_func_t) ulaw_to_float32ne,
    [PA_SAMPLE_S16LE]     = (pa_convert_func_t) pa_sconv_s16le_to_float32ne,
    [PA_SAMPLE_S16BE]     = (pa_convert_func_t) pa_sconv_s16be_to_float32ne,
    [PA_SAMPLE_S32LE]     = (pa_convert_func_t) pa_sconv_s32le_to_float32ne,
    [PA_SAMPLE_S32BE]     = (pa_convert_func_t) pa_sconv_s32be_to_float32ne,
    [PA_SAMPLE_S24LE]     = (pa_convert_func_t) pa_sconv_s24le_to_float32ne,
    [PA_SAMPLE_S24BE]     = (pa_convert_func_t) pa_sconv_s24be_to_float32ne,
    [PA_SAMPLE_S24_32LE]  = (pa_convert_func_t) pa_sconv_s24_32le_to_float32ne,
    [PA_SAMPLE_S24_32BE]  = (pa_convert_func_t) pa_sconv_s24_32be_to_float32ne,
    [PA_SAMPLE_FLOAT32NE] = (pa_convert_func_t) float32ne_to_float32ne,
    [PA_SAMPLE_FLOAT32RE] = (pa_convert_func_t) float32re_to_float32ne,
};

pa_convert_func_t pa_get_convert_to_float32ne_function(pa_sample_format_t f) {
    pa_assert(pa_sample_format_valid(f));

    return to_float32ne_table[f];
}

void pa_set_convert_to_float32ne_function(pa_sample_format_t f, pa_convert_func_t func) {
    pa_assert(pa_sample_format_valid(f));

    to_float32ne_table[f] = func;
}

static pa_convert_func_t from_float32ne_table[] = {
    [PA_SAMPLE_U8]        = (pa_convert_func_t) u8_from_float32ne,
    [PA_SAMPLE_S16LE]     = (pa_convert_func_t) pa_sconv_s16le_from_float32ne,
    [PA_SAMPLE_S16BE]     = (pa_convert_func_t) pa_sconv_s16be_from_float32ne,
    [PA_SAMPLE_S32LE]     = (pa_convert_func_t) pa_sconv_s32le_from_float32ne,
    [PA_SAMPLE_S32BE]     = (pa_convert_func_t) pa_sconv_s32be_from_float32ne,
    [PA_SAMPLE_S24LE]     = (pa_convert_func_t) pa_sconv_s24le_from_float32ne,
    [PA_SAMPLE_S24BE]     = (pa_convert_func_t) pa_sconv_s24be_from_float32ne,
    [PA_SAMPLE_S24_32LE]  = (pa_convert_func_t) pa_sconv_s24_32le_from_float32ne,
    [PA_SAMPLE_S24_32BE]  = (pa_convert_func_t) pa_sconv_s24_32be_from_float32ne,
    [PA_SAMPLE_FLOAT32NE] = (pa_convert_func_t) float32ne_to_float32ne,
    [PA_SAMPLE_FLOAT32RE] = (pa_convert_func_t) float32re_to_float32ne,
    [PA_SAMPLE_ALAW]      = (pa_convert_func_t) alaw_from_float32ne,
    [PA_SAMPLE_ULAW]      = (pa_convert_func_t) ulaw_from_float32ne
};

pa_convert_func_t pa_get_convert_from_float32ne_function(pa_sample_format_t f) {
    pa_assert(pa_sample_format_valid(f));

    return from_float32ne_table[f];
}

void pa_set_convert_from_float32ne_function(pa_sample_format_t f, pa_convert_func_t func) {
    pa_assert(pa_sample_format_valid(f));

    from_float32ne_table[f] = func;
}

static pa_convert_func_t to_s16ne_table[] = {
    [PA_SAMPLE_U8]        = (pa_convert_func_t) u8_to_s16ne,
    [PA_SAMPLE_S16NE]     = (pa_convert_func_t) s16ne_to_s16ne,
    [PA_SAMPLE_S16RE]     = (pa_convert_func_t) s16re_to_s16ne,
    [PA_SAMPLE_FLOAT32BE] = (pa_convert_func_t) pa_sconv_float32be_to_s16ne,
    [PA_SAMPLE_FLOAT32LE] = (pa_convert_func_t) pa_sconv_float32le_to_s16ne,
    [PA_SAMPLE_S32BE]     = (pa_convert_func_t) pa_sconv_s32be_to_s16ne,
    [PA_SAMPLE_S32LE]     = (pa_convert_func_t) pa_sconv_s32le_to_s16ne,
    [PA_SAMPLE_S24BE]     = (pa_convert_func_t) pa_sconv_s24be_to_s16ne,
    [PA_SAMPLE_S24LE]     = (pa_convert_func_t) pa_sconv_s24le_to_s16ne,
    [PA_SAMPLE_S24_32BE]  = (pa_convert_func_t) pa_sconv_s24_32be_to_s16ne,
    [PA_SAMPLE_S24_32LE]  = (pa_convert_func_t) pa_sconv_s24_32le_to_s16ne,
    [PA_SAMPLE_ALAW]      = (pa_convert_func_t) alaw_to_s16ne,
    [PA_SAMPLE_ULAW]      = (pa_convert_func_t) ulaw_to_s16ne
};

pa_convert_func_t pa_get_convert_to_s16ne_function(pa_sample_format_t f) {
    pa_assert(pa_sample_format_valid(f));

    return to_s16ne_table[f];
}

void pa_set_convert_to_s16ne_function(pa_sample_format_t f, pa_convert_func_t func) {
    pa_assert(pa_sample_format_valid(f));

    to_s16ne_table[f] = func;
}

static pa_convert_func_t from_s16ne_table[] = {
    [PA_SAMPLE_U8]        = (pa_convert_func_t) u8_from_s16ne,
    [PA_SAMPLE_S16NE]     = (pa_convert_func_t) s16ne_to_s16ne,
    [PA_SAMPLE_S16RE]     = (pa_convert_func_t) s16re_to_s16ne,
    [PA_SAMPLE_FLOAT32BE] = (pa_convert_func_t) pa_sconv_float32be_from_s16ne,
    [PA_SAMPLE_FLOAT32LE] = (pa_convert_func_t) pa_sconv_float32le_from_s16ne,
    [PA_SAMPLE_S32BE]     = (pa_convert_func_t) pa_sconv_s32be_from_s16ne,
    [PA_SAMPLE_S32LE]     = (pa_convert_func_t) pa_sconv_s32le_from_s16ne,
    [PA_SAMPLE_S24BE]     = (pa_convert_func_t) pa_sconv_s24be_from_s16ne,
    [PA_SAMPLE_S24LE]     = (pa_convert_func_t) pa_sconv_s24le_from_s16ne,
    [PA_SAMPLE_S24_32BE]  = (pa_convert_func_t) pa_sconv_s24_32be_from_s16ne,
    [PA_SAMPLE_S24_32LE]  = (pa_convert_func_t) pa_sconv_s24_32le_from_s16ne,
    [PA_SAMPLE_ALAW]      = (pa_convert_func_t) alaw_from_s16ne,
    [PA_SAMPLE_ULAW]      = (pa_convert_func_t) ulaw_from_s16ne,
};

pa_convert_func_t pa_get_convert_from_s16ne_function(pa_sample_format_t f) {
    pa_assert(pa_sample_format_valid(f));

    return from_s16ne_table[f];
}

void pa_set_convert_from_s16ne_function(pa_sample_format_t f, pa_convert_func_t func) {
    pa_assert(pa_sample_format_valid(f));

    from_s16ne_table[f] = func;
}
