/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB
  Copyright 2013 Peter Meerwald <pmeerw@pmeerw.net>

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

#include <math.h>

#include <pulsecore/sample-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/g711.h>
#include <pulsecore/endianmacros.h>

#include "cpu.h"
#include "mix.h"

#define VOLUME_PADDING 32

static void calc_linear_integer_volume(int32_t linear[], const pa_cvolume *volume) {
    unsigned channel, nchannels, padding;

    pa_assert(linear);
    pa_assert(volume);

    nchannels = volume->channels;

    for (channel = 0; channel < nchannels; channel++)
        linear[channel] = (int32_t) lrint(pa_sw_volume_to_linear(volume->values[channel]) * 0x10000);

    for (padding = 0; padding < VOLUME_PADDING; padding++, channel++)
        linear[channel] = linear[padding];
}

static void calc_linear_float_volume(float linear[], const pa_cvolume *volume) {
    unsigned channel, nchannels, padding;

    pa_assert(linear);
    pa_assert(volume);

    nchannels = volume->channels;

    for (channel = 0; channel < nchannels; channel++)
        linear[channel] = (float) pa_sw_volume_to_linear(volume->values[channel]);

    for (padding = 0; padding < VOLUME_PADDING; padding++, channel++)
        linear[channel] = linear[padding];
}

static void calc_linear_integer_stream_volumes(pa_mix_info streams[], unsigned nstreams, const pa_cvolume *volume, const pa_sample_spec *spec) {
    unsigned k, channel;
    float linear[PA_CHANNELS_MAX + VOLUME_PADDING];

    pa_assert(streams);
    pa_assert(spec);
    pa_assert(volume);

    calc_linear_float_volume(linear, volume);

    for (k = 0; k < nstreams; k++) {

        for (channel = 0; channel < spec->channels; channel++) {
            pa_mix_info *m = streams + k;
            m->linear[channel].i = (int32_t) lrint(pa_sw_volume_to_linear(m->volume.values[channel]) * linear[channel] * 0x10000);
        }
    }
}

static void calc_linear_float_stream_volumes(pa_mix_info streams[], unsigned nstreams, const pa_cvolume *volume, const pa_sample_spec *spec) {
    unsigned k, channel;
    float linear[PA_CHANNELS_MAX + VOLUME_PADDING];

    pa_assert(streams);
    pa_assert(spec);
    pa_assert(volume);

    calc_linear_float_volume(linear, volume);

    for (k = 0; k < nstreams; k++) {

        for (channel = 0; channel < spec->channels; channel++) {
            pa_mix_info *m = streams + k;
            m->linear[channel].f = (float) (pa_sw_volume_to_linear(m->volume.values[channel]) * linear[channel]);
        }
    }
}

typedef void (*pa_calc_stream_volumes_func_t) (pa_mix_info streams[], unsigned nstreams, const pa_cvolume *volume, const pa_sample_spec *spec);

static const pa_calc_stream_volumes_func_t calc_stream_volumes_table[] = {
  [PA_SAMPLE_U8]        = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_ALAW]      = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_ULAW]      = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S16LE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S16BE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_FLOAT32LE] = (pa_calc_stream_volumes_func_t) calc_linear_float_stream_volumes,
  [PA_SAMPLE_FLOAT32BE] = (pa_calc_stream_volumes_func_t) calc_linear_float_stream_volumes,
  [PA_SAMPLE_S32LE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S32BE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S24LE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S24BE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S24_32LE]  = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S24_32BE]  = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes
};

/* special case: mix 2 s16ne streams, 1 channel each */
static void pa_mix2_ch1_s16ne(pa_mix_info streams[], int16_t *data, unsigned length) {
    const int16_t *ptr0 = streams[0].ptr;
    const int16_t *ptr1 = streams[1].ptr;

    const int32_t cv0 = streams[0].linear[0].i;
    const int32_t cv1 = streams[1].linear[0].i;

    length /= sizeof(int16_t);

    for (; length > 0; length--) {
        int32_t sum;

        sum = pa_mult_s16_volume(*ptr0++, cv0);
        sum += pa_mult_s16_volume(*ptr1++, cv1);

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;
    }
}

/* special case: mix 2 s16ne streams, 2 channels each */
static void pa_mix2_ch2_s16ne(pa_mix_info streams[], int16_t *data, unsigned length) {
    const int16_t *ptr0 = streams[0].ptr;
    const int16_t *ptr1 = streams[1].ptr;

    length /= sizeof(int16_t) * 2;

    for (; length > 0; length--) {
        int32_t sum;

        sum = pa_mult_s16_volume(*ptr0++, streams[0].linear[0].i);
        sum += pa_mult_s16_volume(*ptr1++, streams[1].linear[0].i);

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;

        sum = pa_mult_s16_volume(*ptr0++, streams[0].linear[1].i);
        sum += pa_mult_s16_volume(*ptr1++, streams[1].linear[1].i);

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;
    }
}

/* special case: mix 2 s16ne streams */
static void pa_mix2_s16ne(pa_mix_info streams[], unsigned channels, int16_t *data, unsigned length) {
    const int16_t *ptr0 = streams[0].ptr;
    const int16_t *ptr1 = streams[1].ptr;
    unsigned channel = 0;

    length /= sizeof(int16_t);

    for (; length > 0; length--) {
        int32_t sum;

        sum = pa_mult_s16_volume(*ptr0++, streams[0].linear[channel].i);
        sum += pa_mult_s16_volume(*ptr1++, streams[1].linear[channel].i);

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

/* special case: mix s16ne streams, 2 channels each */
static void pa_mix_ch2_s16ne(pa_mix_info streams[], unsigned nstreams, int16_t *data, unsigned length) {

    length /= sizeof(int16_t) * 2;

    for (; length > 0; length--) {
        int32_t sum0 = 0, sum1 = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv0 = m->linear[0].i;
            int32_t cv1 = m->linear[1].i;

            sum0 += pa_mult_s16_volume(*((int16_t*) m->ptr), cv0);
            m->ptr = (uint8_t*) m->ptr + sizeof(int16_t);

            sum1 += pa_mult_s16_volume(*((int16_t*) m->ptr), cv1);
            m->ptr = (uint8_t*) m->ptr + sizeof(int16_t);
        }

        *data++ = PA_CLAMP_UNLIKELY(sum0, -0x8000, 0x7FFF);
        *data++ = PA_CLAMP_UNLIKELY(sum1, -0x8000, 0x7FFF);
    }
}

static void pa_mix_generic_s16ne(pa_mix_info streams[], unsigned nstreams, unsigned channels, int16_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(int16_t);

    for (; length > 0; length--) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0))
                sum += pa_mult_s16_volume(*((int16_t*) m->ptr), cv);
            m->ptr = (uint8_t*) m->ptr + sizeof(int16_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s16ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, int16_t *data, unsigned length) {
    if (nstreams == 2 && channels == 1)
        pa_mix2_ch1_s16ne(streams, data, length);
    else if (nstreams == 2 && channels == 2)
        pa_mix2_ch2_s16ne(streams, data, length);
    else if (nstreams == 2)
        pa_mix2_s16ne(streams, channels, data, length);
    else if (channels == 2)
        pa_mix_ch2_s16ne(streams, nstreams, data, length);
    else
        pa_mix_generic_s16ne(streams, nstreams, channels, data, length);
}

static void pa_mix_s16re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, int16_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(int16_t);

    for (; length > 0; length--, data++) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0))
                sum += pa_mult_s16_volume(PA_INT16_SWAP(*((int16_t*) m->ptr)), cv);
            m->ptr = (uint8_t*) m->ptr + sizeof(int16_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data = PA_INT16_SWAP((int16_t) sum);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s32ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, int32_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(int32_t);

    for (; length > 0; length--, data++) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = *((int32_t*) m->ptr);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(int32_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        *data = (int32_t) sum;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s32re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, int32_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(int32_t);

    for (; length > 0; length--, data++) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = PA_INT32_SWAP(*((int32_t*) m->ptr));
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(int32_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        *data = PA_INT32_SWAP((int32_t) sum);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s24ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    for (; length > 0; length -= 3, data += 3) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) (PA_READ24NE(m->ptr) << 8);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + 3;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        PA_WRITE24NE(data, ((uint32_t) sum) >> 8);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s24re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    for (; length > 0; length -= 3, data += 3) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) (PA_READ24RE(m->ptr) << 8);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + 3;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        PA_WRITE24RE(data, ((uint32_t) sum) >> 8);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s24_32ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint32_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint32_t);

    for (; length > 0; length--, data++) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) (*((uint32_t*)m->ptr) << 8);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(int32_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        *data = ((uint32_t) (int32_t) sum) >> 8;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s24_32re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint32_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint32_t);

    for (; length > 0; length--, data++) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) (PA_UINT32_SWAP(*((uint32_t*) m->ptr)) << 8);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(int32_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        *data = PA_INT32_SWAP(((uint32_t) (int32_t) sum) >> 8);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_u8_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint8_t);

    for (; length > 0; length--, data++) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t v, cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) *((uint8_t*) m->ptr) - 0x80;
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + 1;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80, 0x7F);
        *data = (uint8_t) (sum + 0x80);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_ulaw_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint8_t);

    for (; length > 0; length--, data++) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0))
                sum += pa_mult_s16_volume(st_ulaw2linear16(*((uint8_t*) m->ptr)), cv);
            m->ptr = (uint8_t*) m->ptr + 1;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data = (uint8_t) st_14linear2ulaw((int16_t) sum >> 2);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_alaw_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint8_t);

    for (; length > 0; length--, data++) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0))
                sum += pa_mult_s16_volume(st_alaw2linear16(*((uint8_t*) m->ptr)), cv);
            m->ptr = (uint8_t*) m->ptr + 1;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data = (uint8_t) st_13linear2alaw((int16_t) sum >> 3);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_float32ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, float *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(float);

    for (; length > 0; length--, data++) {
        float sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            float v, cv = m->linear[channel].f;

            if (PA_LIKELY(cv > 0)) {
                v = *((float*) m->ptr);
                v *= cv;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(float);
        }

        *data = sum;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_float32re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, float *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(float);

    for (; length > 0; length--, data++) {
        float sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            float cv = m->linear[channel].f;

            if (PA_LIKELY(cv > 0))
                sum += PA_READ_FLOAT32RE(m->ptr) * cv;
            m->ptr = (uint8_t*) m->ptr + sizeof(float);
        }

        PA_WRITE_FLOAT32RE(data, sum);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static pa_do_mix_func_t do_mix_table[] = {
    [PA_SAMPLE_U8]        = (pa_do_mix_func_t) pa_mix_u8_c,
    [PA_SAMPLE_ALAW]      = (pa_do_mix_func_t) pa_mix_alaw_c,
    [PA_SAMPLE_ULAW]      = (pa_do_mix_func_t) pa_mix_ulaw_c,
    [PA_SAMPLE_S16NE]     = (pa_do_mix_func_t) pa_mix_s16ne_c,
    [PA_SAMPLE_S16RE]     = (pa_do_mix_func_t) pa_mix_s16re_c,
    [PA_SAMPLE_FLOAT32NE] = (pa_do_mix_func_t) pa_mix_float32ne_c,
    [PA_SAMPLE_FLOAT32RE] = (pa_do_mix_func_t) pa_mix_float32re_c,
    [PA_SAMPLE_S32NE]     = (pa_do_mix_func_t) pa_mix_s32ne_c,
    [PA_SAMPLE_S32RE]     = (pa_do_mix_func_t) pa_mix_s32re_c,
    [PA_SAMPLE_S24NE]     = (pa_do_mix_func_t) pa_mix_s24ne_c,
    [PA_SAMPLE_S24RE]     = (pa_do_mix_func_t) pa_mix_s24re_c,
    [PA_SAMPLE_S24_32NE]  = (pa_do_mix_func_t) pa_mix_s24_32ne_c,
    [PA_SAMPLE_S24_32RE]  = (pa_do_mix_func_t) pa_mix_s24_32re_c
};

void pa_mix_func_init(const pa_cpu_info *cpu_info) {
    if (cpu_info->force_generic_code)
        do_mix_table[PA_SAMPLE_S16NE] = (pa_do_mix_func_t) pa_mix_generic_s16ne;
    else
        do_mix_table[PA_SAMPLE_S16NE] = (pa_do_mix_func_t) pa_mix_s16ne_c;
}

size_t pa_mix(
        pa_mix_info streams[],
        unsigned nstreams,
        void *data,
        size_t length,
        const pa_sample_spec *spec,
        const pa_cvolume *volume,
        bool mute) {

    pa_cvolume full_volume;
    unsigned k;

    pa_assert(streams);
    pa_assert(data);
    pa_assert(length);
    pa_assert(spec);
    pa_assert(nstreams > 1);

    if (!volume)
        volume = pa_cvolume_reset(&full_volume, spec->channels);

    if (mute || pa_cvolume_is_muted(volume)) {
        pa_silence_memory(data, length, spec);
        return length;
    }

    for (k = 0; k < nstreams; k++) {
        pa_assert(length <= streams[k].chunk.length);
        streams[k].ptr = pa_memblock_acquire_chunk(&streams[k].chunk);
    }

    calc_stream_volumes_table[spec->format](streams, nstreams, volume, spec);
    do_mix_table[spec->format](streams, nstreams, spec->channels, data, length);

    for (k = 0; k < nstreams; k++)
        pa_memblock_release(streams[k].chunk.memblock);

    return length;
}

pa_do_mix_func_t pa_get_mix_func(pa_sample_format_t f) {
    pa_assert(pa_sample_format_valid(f));

    return do_mix_table[f];
}

void pa_set_mix_func(pa_sample_format_t f, pa_do_mix_func_t func) {
    pa_assert(pa_sample_format_valid(f));

    do_mix_table[f] = func;
}

typedef union {
  float f;
  uint32_t i;
} volume_val;

typedef void (*pa_calc_volume_func_t) (void *volumes, const pa_cvolume *volume);

static const pa_calc_volume_func_t calc_volume_table[] = {
  [PA_SAMPLE_U8]        = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_ALAW]      = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_ULAW]      = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S16LE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S16BE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_FLOAT32LE] = (pa_calc_volume_func_t) calc_linear_float_volume,
  [PA_SAMPLE_FLOAT32BE] = (pa_calc_volume_func_t) calc_linear_float_volume,
  [PA_SAMPLE_S32LE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S32BE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S24LE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S24BE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S24_32LE]  = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S24_32BE]  = (pa_calc_volume_func_t) calc_linear_integer_volume
};

void pa_volume_memchunk(
        pa_memchunk*c,
        const pa_sample_spec *spec,
        const pa_cvolume *volume) {

    void *ptr;
    volume_val linear[PA_CHANNELS_MAX + VOLUME_PADDING];
    pa_do_volume_func_t do_volume;

    pa_assert(c);
    pa_assert(spec);
    pa_assert(pa_sample_spec_valid(spec));
    pa_assert(pa_frame_aligned(c->length, spec));
    pa_assert(volume);

    if (pa_memblock_is_silence(c->memblock))
        return;

    if (pa_cvolume_is_norm(volume))
        return;

    if (pa_cvolume_is_muted(volume)) {
        pa_silence_memchunk(c, spec);
        return;
    }

    do_volume = pa_get_volume_func(spec->format);
    pa_assert(do_volume);

    calc_volume_table[spec->format] ((void *)linear, volume);

    ptr = pa_memblock_acquire_chunk(c);

    do_volume(ptr, (void *)linear, spec->channels, c->length);

    pa_memblock_release(c->memblock);
}
