/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <speex/speex_resampler.h>
#include <math.h>

#include <pulsecore/once.h>
#include <pulsecore/resampler.h>

bool pa_speex_is_fixed_point(void) {
    static bool result = false;
    PA_ONCE_BEGIN {
        float f_out = -1.0f, f_in = 1.0f;
        spx_uint32_t in_len = 1, out_len = 1;
        SpeexResamplerState *s;

        pa_assert_se(s = speex_resampler_init(1, 1, 1,
            SPEEX_RESAMPLER_QUALITY_MIN, NULL));

        /* feed one sample that is too soft for fixed-point speex */
        pa_assert_se(speex_resampler_process_float(s, 0, &f_in, &in_len,
            &f_out, &out_len) == RESAMPLER_ERR_SUCCESS);

        /* expecting sample has been processed, one sample output */
        pa_assert_se(in_len == 1 && out_len == 1);

        /* speex compiled with --enable-fixed-point will output 0.0 due to insufficient precision */
        if (fabsf(f_out) < 0.00001f)
            result = true;

        speex_resampler_destroy(s);
    } PA_ONCE_END;
    return result;
}


static unsigned speex_resample_float(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    float *in, *out;
    uint32_t inf = in_n_frames, outf = *out_n_frames;
    SpeexResamplerState *state;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    state = r->impl.data;

    in = pa_memblock_acquire_chunk(input);
    out = pa_memblock_acquire_chunk(output);

    /* Strictly speaking, speex resampler expects its input
     * to be normalized to the [-32768.0 .. 32767.0] range.
     * This matters if speex has been compiled with --enable-fixed-point,
     * because such speex will round the samples to the nearest
     * integer. speex with --enable-fixed-point is therefore incompatible
     * with PulseAudio's floating-point sample range [-1 .. 1]. speex
     * without --enable-fixed-point works fine with this range.
     * Care has been taken to call speex_resample_float() only
     * for speex compiled without --enable-fixed-point.
     */
    pa_assert_se(speex_resampler_process_interleaved_float(state, in, &inf, out, &outf) == 0);

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    pa_assert(inf == in_n_frames);
    *out_n_frames = outf;

    return 0;
}

static unsigned speex_resample_int(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    int16_t *in, *out;
    uint32_t inf = in_n_frames, outf = *out_n_frames;
    SpeexResamplerState *state;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    state = r->impl.data;

    in = pa_memblock_acquire_chunk(input);
    out = pa_memblock_acquire_chunk(output);

    pa_assert_se(speex_resampler_process_interleaved_int(state, in, &inf, out, &outf) == 0);

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    pa_assert(inf == in_n_frames);
    *out_n_frames = outf;

    return 0;
}

static void speex_update_rates(pa_resampler *r) {
    SpeexResamplerState *state;
    pa_assert(r);

    state = r->impl.data;

    pa_assert_se(speex_resampler_set_rate(state, r->i_ss.rate, r->o_ss.rate) == 0);
}

static void speex_reset(pa_resampler *r) {
    SpeexResamplerState *state;
    pa_assert(r);

    state = r->impl.data;

    pa_assert_se(speex_resampler_reset_mem(state) == 0);
}

static void speex_free(pa_resampler *r) {
    SpeexResamplerState *state;
    pa_assert(r);

    state = r->impl.data;
    if (!state)
        return;

    speex_resampler_destroy(state);
}

int pa_resampler_speex_init(pa_resampler *r) {
    int q, err;
    SpeexResamplerState *state;

    pa_assert(r);

    r->impl.free = speex_free;
    r->impl.update_rates = speex_update_rates;
    r->impl.reset = speex_reset;

    if (r->method >= PA_RESAMPLER_SPEEX_FIXED_BASE && r->method <= PA_RESAMPLER_SPEEX_FIXED_MAX) {

        q = r->method - PA_RESAMPLER_SPEEX_FIXED_BASE;
        r->impl.resample = speex_resample_int;

    } else {
        pa_assert(r->method >= PA_RESAMPLER_SPEEX_FLOAT_BASE && r->method <= PA_RESAMPLER_SPEEX_FLOAT_MAX);

        q = r->method - PA_RESAMPLER_SPEEX_FLOAT_BASE;
        r->impl.resample = speex_resample_float;
    }

    pa_log_info("Choosing speex quality setting %i.", q);

    if (!(state = speex_resampler_init(r->work_channels, r->i_ss.rate, r->o_ss.rate, q, &err)))
        return -1;

    r->impl.data = state;

    return 0;
}
