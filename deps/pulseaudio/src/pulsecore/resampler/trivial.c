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

#include <pulse/xmalloc.h>

#include <pulsecore/resampler.h>

struct trivial_data { /* data specific to the trivial resampler */
    unsigned o_counter;
    unsigned i_counter;
};

static unsigned trivial_resample(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    unsigned i_index, o_index;
    void *src, *dst;
    struct trivial_data *trivial_data;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    trivial_data = r->impl.data;

    src = pa_memblock_acquire_chunk(input);
    dst = pa_memblock_acquire_chunk(output);

    for (o_index = 0;; o_index++, trivial_data->o_counter++) {
        i_index = ((uint64_t) trivial_data->o_counter * r->i_ss.rate) / r->o_ss.rate;
        i_index = i_index > trivial_data->i_counter ? i_index - trivial_data->i_counter : 0;

        if (i_index >= in_n_frames)
            break;

        pa_assert_fp(o_index * r->w_fz < pa_memblock_get_length(output->memblock));

        memcpy((uint8_t*) dst + r->w_fz * o_index, (uint8_t*) src + r->w_fz * i_index, (int) r->w_fz);
    }

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    *out_n_frames = o_index;

    trivial_data->i_counter += in_n_frames;

    /* Normalize counters */
    while (trivial_data->i_counter >= r->i_ss.rate) {
        pa_assert(trivial_data->o_counter >= r->o_ss.rate);

        trivial_data->i_counter -= r->i_ss.rate;
        trivial_data->o_counter -= r->o_ss.rate;
    }

    return 0;
}

static void trivial_update_rates_or_reset(pa_resampler *r) {
    struct trivial_data *trivial_data;
    pa_assert(r);

    trivial_data = r->impl.data;

    trivial_data->i_counter = 0;
    trivial_data->o_counter = 0;
}

int pa_resampler_trivial_init(pa_resampler *r) {
    struct trivial_data *trivial_data;
    pa_assert(r);

    trivial_data = pa_xnew0(struct trivial_data, 1);

    r->impl.resample = trivial_resample;
    r->impl.update_rates = trivial_update_rates_or_reset;
    r->impl.reset = trivial_update_rates_or_reset;
    r->impl.data = trivial_data;

    return 0;
}
