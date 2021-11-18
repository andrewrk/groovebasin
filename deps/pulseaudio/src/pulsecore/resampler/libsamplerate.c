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

#include <samplerate.h>

#include <pulsecore/resampler.h>

static unsigned libsamplerate_resample(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    SRC_DATA data;
    SRC_STATE *state;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    state = r->impl.data;
    memset(&data, 0, sizeof(data));

    data.data_in = pa_memblock_acquire_chunk(input);
    data.input_frames = (long int) in_n_frames;

    data.data_out = pa_memblock_acquire_chunk(output);
    data.output_frames = (long int) *out_n_frames;

    data.src_ratio = (double) r->o_ss.rate / r->i_ss.rate;
    data.end_of_input = 0;

    pa_assert_se(src_process(state, &data) == 0);

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    *out_n_frames = (unsigned) data.output_frames_gen;

    return in_n_frames - data.input_frames_used;
}

static void libsamplerate_update_rates(pa_resampler *r) {
    SRC_STATE *state;
    pa_assert(r);

    state = r->impl.data;
    pa_assert_se(src_set_ratio(state, (double) r->o_ss.rate / r->i_ss.rate) == 0);
}

static void libsamplerate_reset(pa_resampler *r) {
    SRC_STATE *state;
    pa_assert(r);

    state = r->impl.data;
    pa_assert_se(src_reset(state) == 0);
}

static void libsamplerate_free(pa_resampler *r) {
    SRC_STATE *state;
    pa_assert(r);

    state = r->impl.data;
    if (state)
        src_delete(state);
}

int pa_resampler_libsamplerate_init(pa_resampler *r) {
    int err;
    SRC_STATE *state;

    pa_assert(r);

    if (!(state = src_new(r->method, r->work_channels, &err)))
        return -1;

    r->impl.free = libsamplerate_free;
    r->impl.update_rates = libsamplerate_update_rates;
    r->impl.resample = libsamplerate_resample;
    r->impl.reset = libsamplerate_reset;
    r->impl.data = state;

    return 0;
}
