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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#include <pulse/timeval.h>

#include <pulsecore/log.h>
#include <pulsecore/core-error.h>
#include <pulsecore/macro.h>
#include <pulsecore/g711.h>
#include <pulsecore/core-util.h>
#include <pulsecore/endianmacros.h>

#include "sample-util.h"

#define PA_SILENCE_MAX (pa_page_size()*16)

pa_memblock *pa_silence_memblock(pa_memblock* b, const pa_sample_spec *spec) {
    void *data;

    pa_assert(b);
    pa_assert(spec);

    data = pa_memblock_acquire(b);
    pa_silence_memory(data, pa_memblock_get_length(b), spec);
    pa_memblock_release(b);

    return b;
}

pa_memchunk* pa_silence_memchunk(pa_memchunk *c, const pa_sample_spec *spec) {
    void *data;

    pa_assert(c);
    pa_assert(c->memblock);
    pa_assert(spec);

    data = pa_memblock_acquire(c->memblock);
    pa_silence_memory((uint8_t*) data+c->index, c->length, spec);
    pa_memblock_release(c->memblock);

    return c;
}

static uint8_t silence_byte(pa_sample_format_t format) {
    switch (format) {
        case PA_SAMPLE_U8:
            return 0x80;
        case PA_SAMPLE_S16LE:
        case PA_SAMPLE_S16BE:
        case PA_SAMPLE_S32LE:
        case PA_SAMPLE_S32BE:
        case PA_SAMPLE_FLOAT32LE:
        case PA_SAMPLE_FLOAT32BE:
        case PA_SAMPLE_S24LE:
        case PA_SAMPLE_S24BE:
        case PA_SAMPLE_S24_32LE:
        case PA_SAMPLE_S24_32BE:
            return 0;
        case PA_SAMPLE_ALAW:
            return 0xd5;
        case PA_SAMPLE_ULAW:
            return 0xff;
        default:
            pa_assert_not_reached();
    }
}

void* pa_silence_memory(void *p, size_t length, const pa_sample_spec *spec) {
    pa_assert(p);
    pa_assert(length > 0);
    pa_assert(spec);

    memset(p, silence_byte(spec->format), length);
    return p;
}

size_t pa_frame_align(size_t l, const pa_sample_spec *ss) {
    size_t fs;

    pa_assert(ss);

    fs = pa_frame_size(ss);

    return (l/fs) * fs;
}

bool pa_frame_aligned(size_t l, const pa_sample_spec *ss) {
    size_t fs;

    pa_assert(ss);

    fs = pa_frame_size(ss);

    return l % fs == 0;
}

void pa_interleave(const void *src[], unsigned channels, void *dst, size_t ss, unsigned n) {
    unsigned c;
    size_t fs;

    pa_assert(src);
    pa_assert(channels > 0);
    pa_assert(dst);
    pa_assert(ss > 0);
    pa_assert(n > 0);

    fs = ss * channels;

    for (c = 0; c < channels; c++) {
        unsigned j;
        void *d;
        const void *s;

        s = src[c];
        d = (uint8_t*) dst + c * ss;

        for (j = 0; j < n; j ++) {
            memcpy(d, s, (int) ss);
            s = (uint8_t*) s + ss;
            d = (uint8_t*) d + fs;
        }
    }
}

void pa_deinterleave(const void *src, void *dst[], unsigned channels, size_t ss, unsigned n) {
    size_t fs;
    unsigned c;

    pa_assert(src);
    pa_assert(dst);
    pa_assert(channels > 0);
    pa_assert(ss > 0);
    pa_assert(n > 0);

    fs = ss * channels;

    for (c = 0; c < channels; c++) {
        unsigned j;
        const void *s;
        void *d;

        s = (uint8_t*) src + c * ss;
        d = dst[c];

        for (j = 0; j < n; j ++) {
            memcpy(d, s, (int) ss);
            s = (uint8_t*) s + fs;
            d = (uint8_t*) d + ss;
        }
    }
}

static pa_memblock *silence_memblock_new(pa_mempool *pool, uint8_t c) {
    pa_memblock *b;
    size_t length;
    void *data;

    pa_assert(pool);

    length = PA_MIN(pa_mempool_block_size_max(pool), PA_SILENCE_MAX);

    b = pa_memblock_new(pool, length);

    data = pa_memblock_acquire(b);
    memset(data, c, length);
    pa_memblock_release(b);

    pa_memblock_set_is_silence(b, true);

    return b;
}

void pa_silence_cache_init(pa_silence_cache *cache) {
    pa_assert(cache);

    memset(cache, 0, sizeof(pa_silence_cache));
}

void pa_silence_cache_done(pa_silence_cache *cache) {
    pa_sample_format_t f;
    pa_assert(cache);

    for (f = 0; f < PA_SAMPLE_MAX; f++)
        if (cache->blocks[f])
            pa_memblock_unref(cache->blocks[f]);

    memset(cache, 0, sizeof(pa_silence_cache));
}

pa_memchunk* pa_silence_memchunk_get(pa_silence_cache *cache, pa_mempool *pool, pa_memchunk* ret, const pa_sample_spec *spec, size_t length) {
    pa_memblock *b;
    size_t l;

    pa_assert(cache);
    pa_assert(pa_sample_spec_valid(spec));

    if (!(b = cache->blocks[spec->format]))

        switch (spec->format) {
            case PA_SAMPLE_U8:
                cache->blocks[PA_SAMPLE_U8] = b = silence_memblock_new(pool, 0x80);
                break;
            case PA_SAMPLE_S16LE:
            case PA_SAMPLE_S16BE:
            case PA_SAMPLE_S32LE:
            case PA_SAMPLE_S32BE:
            case PA_SAMPLE_S24LE:
            case PA_SAMPLE_S24BE:
            case PA_SAMPLE_S24_32LE:
            case PA_SAMPLE_S24_32BE:
            case PA_SAMPLE_FLOAT32LE:
            case PA_SAMPLE_FLOAT32BE:
                cache->blocks[PA_SAMPLE_S16LE] = b = silence_memblock_new(pool, 0);
                cache->blocks[PA_SAMPLE_S16BE] = pa_memblock_ref(b);
                cache->blocks[PA_SAMPLE_S32LE] = pa_memblock_ref(b);
                cache->blocks[PA_SAMPLE_S32BE] = pa_memblock_ref(b);
                cache->blocks[PA_SAMPLE_S24LE] = pa_memblock_ref(b);
                cache->blocks[PA_SAMPLE_S24BE] = pa_memblock_ref(b);
                cache->blocks[PA_SAMPLE_S24_32LE] = pa_memblock_ref(b);
                cache->blocks[PA_SAMPLE_S24_32BE] = pa_memblock_ref(b);
                cache->blocks[PA_SAMPLE_FLOAT32LE] = pa_memblock_ref(b);
                cache->blocks[PA_SAMPLE_FLOAT32BE] = pa_memblock_ref(b);
                break;
            case PA_SAMPLE_ALAW:
                cache->blocks[PA_SAMPLE_ALAW] = b = silence_memblock_new(pool, 0xd5);
                break;
            case PA_SAMPLE_ULAW:
                cache->blocks[PA_SAMPLE_ULAW] = b = silence_memblock_new(pool, 0xff);
                break;
            default:
                pa_assert_not_reached();
    }

    pa_assert(b);

    ret->memblock = pa_memblock_ref(b);

    l = pa_memblock_get_length(b);
    if (length > l || length == 0)
        length = l;

    ret->length = pa_frame_align(length, spec);
    ret->index = 0;

    return ret;
}

void pa_sample_clamp(pa_sample_format_t format, void *dst, size_t dstr, const void *src, size_t sstr, unsigned n) {
    const float *s;
    float *d;

    s = src; d = dst;

    if (format == PA_SAMPLE_FLOAT32NE) {
        for (; n > 0; n--) {
            float f;

            f = *s;
            *d = PA_CLAMP_UNLIKELY(f, -1.0f, 1.0f);

            s = (const float*) ((const uint8_t*) s + sstr);
            d = (float*) ((uint8_t*) d + dstr);
        }
    } else {
        pa_assert(format == PA_SAMPLE_FLOAT32RE);

        for (; n > 0; n--) {
            float f;

            f = PA_READ_FLOAT32RE(s);
            f = PA_CLAMP_UNLIKELY(f, -1.0f, 1.0f);
            PA_WRITE_FLOAT32RE(d, f);

            s = (const float*) ((const uint8_t*) s + sstr);
            d = (float*) ((uint8_t*) d + dstr);
        }
    }
}

/* Similar to pa_bytes_to_usec() but rounds up, not down */

pa_usec_t pa_bytes_to_usec_round_up(uint64_t length, const pa_sample_spec *spec) {
    size_t fs;
    pa_usec_t usec;

    pa_assert(spec);

    fs = pa_frame_size(spec);
    length = (length + fs - 1) / fs;

    usec = (pa_usec_t) length * PA_USEC_PER_SEC;

    return (usec + spec->rate - 1) / spec->rate;
}

/* Similar to pa_usec_to_bytes() but rounds up, not down */

size_t pa_usec_to_bytes_round_up(pa_usec_t t, const pa_sample_spec *spec) {
    uint64_t u;
    pa_assert(spec);

    u = (uint64_t) t * (uint64_t) spec->rate;

    u = (u + PA_USEC_PER_SEC - 1) / PA_USEC_PER_SEC;

    u *= pa_frame_size(spec);

    return (size_t) u;
}

void pa_memchunk_dump_to_file(pa_memchunk *c, const char *fn) {
    FILE *f;
    void *p;

    pa_assert(c);
    pa_assert(fn);

    /* Only for debugging purposes */

    f = pa_fopen_cloexec(fn, "a");

    if (!f) {
        pa_log_warn("Failed to open '%s': %s", fn, pa_cstrerror(errno));
        return;
    }

    p = pa_memblock_acquire(c->memblock);

    if (fwrite((uint8_t*) p + c->index, 1, c->length, f) != c->length)
        pa_log_warn("Failed to write to '%s': %s", fn, pa_cstrerror(errno));

    pa_memblock_release(c->memblock);

    fclose(f);
}

static void calc_sine(float *f, size_t l, double freq) {
    size_t i;

    l /= sizeof(float);

    for (i = 0; i < l; i++)
        *(f++) = (float) 0.5f * sin((double) i*M_PI*2*freq / (double) l);
}

void pa_memchunk_sine(pa_memchunk *c, pa_mempool *pool, unsigned rate, unsigned freq) {
    size_t l;
    unsigned gcd, n;
    void *p;

    pa_memchunk_reset(c);

    gcd = pa_gcd(rate, freq);
    n = rate / gcd;

    l = pa_mempool_block_size_max(pool) / sizeof(float);

    l /= n;
    if (l <= 0) l = 1;
    l *= n;

    c->length = l * sizeof(float);
    c->memblock = pa_memblock_new(pool, c->length);

    p = pa_memblock_acquire(c->memblock);
    calc_sine(p, c->length, freq * l / rate);
    pa_memblock_release(c->memblock);
}

size_t pa_convert_size(size_t size, const pa_sample_spec *from, const pa_sample_spec *to) {
    pa_usec_t usec;

    pa_assert(from);
    pa_assert(to);

    usec = pa_bytes_to_usec_round_up(size, from);
    return pa_usec_to_bytes_round_up(usec, to);
}
