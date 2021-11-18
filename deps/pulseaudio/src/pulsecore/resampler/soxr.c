/***
  This file is part of PulseAudio.

  Copyright 2014, 2015 Andrey Semashev

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

#include <stddef.h>
#include <soxr.h>

#include <pulsecore/resampler.h>

static unsigned resampler_soxr_resample(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames,
                                        pa_memchunk *output, unsigned *out_n_frames) {
    soxr_t state;
    void *in, *out;
    size_t consumed = 0, produced = 0;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    state = r->impl.data;
    pa_assert(state);

    in = pa_memblock_acquire_chunk(input);
    out = pa_memblock_acquire_chunk(output);

    pa_assert_se(soxr_process(state, in, in_n_frames, &consumed, out, *out_n_frames, &produced) == 0);

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    *out_n_frames = produced;

    return in_n_frames - consumed;
}

static void resampler_soxr_free(pa_resampler *r) {
    pa_assert(r);

    if (!r->impl.data)
        return;

    soxr_delete(r->impl.data);
    r->impl.data = NULL;
}

static void resampler_soxr_reset(pa_resampler *r) {
#if SOXR_THIS_VERSION >= SOXR_VERSION(0, 1, 2)
    pa_assert(r);

    soxr_clear(r->impl.data);
#else
    /* With libsoxr prior to 0.1.2 soxr_clear() makes soxr_process() crash afterwards,
     * so don't use this function and re-create the context instead. */
    soxr_t old_state;

    pa_assert(r);

    old_state = r->impl.data;
    r->impl.data = NULL;

    if (pa_resampler_soxr_init(r) == 0) {
        if (old_state)
            soxr_delete(old_state);
    } else {
        r->impl.data = old_state;
        pa_log_error("Failed to reset libsoxr context");
    }
#endif
}

static void resampler_soxr_update_rates(pa_resampler *r) {
    soxr_t old_state;

    pa_assert(r);

    /* There is no update method in libsoxr,
     * so just re-create the resampler context */

    old_state = r->impl.data;
    r->impl.data = NULL;

    if (pa_resampler_soxr_init(r) == 0) {
        if (old_state)
            soxr_delete(old_state);
    } else {
        r->impl.data = old_state;
        pa_log_error("Failed to update libsoxr sample rates");
    }
}

int pa_resampler_soxr_init(pa_resampler *r) {
    soxr_t state;
    soxr_datatype_t io_format;
    soxr_io_spec_t io_spec;
    soxr_runtime_spec_t runtime_spec;
    unsigned long quality_recipe;
    soxr_quality_spec_t quality;
    soxr_error_t err = NULL;

    pa_assert(r);

    switch (r->work_format) {
        case PA_SAMPLE_S16NE:
            io_format = SOXR_INT16_I;
            break;
        case PA_SAMPLE_FLOAT32NE:
            io_format = SOXR_FLOAT32_I;
            break;
        default:
            pa_assert_not_reached();
    }

    io_spec = soxr_io_spec(io_format, io_format);

    /* Resample in one thread. Multithreading makes
     * performance worse with small chunks of audio. */
    runtime_spec = soxr_runtime_spec(1);

    switch (r->method) {
        case PA_RESAMPLER_SOXR_MQ:
            quality_recipe = SOXR_MQ | SOXR_LINEAR_PHASE;
            break;
        case PA_RESAMPLER_SOXR_HQ:
            quality_recipe = SOXR_HQ | SOXR_LINEAR_PHASE;
            break;
        case PA_RESAMPLER_SOXR_VHQ:
            quality_recipe = SOXR_VHQ | SOXR_LINEAR_PHASE;
            break;
        default:
            pa_assert_not_reached();
    }

    quality = soxr_quality_spec(quality_recipe, 0);

    state = soxr_create(r->i_ss.rate, r->o_ss.rate, r->work_channels, &err, &io_spec, &quality, &runtime_spec);
    if (!state) {
        pa_log_error("Failed to create libsoxr resampler context: %s.", (err ? err : "[unknown error]"));
        return -1;
    }

    r->impl.free = resampler_soxr_free;
    r->impl.reset = resampler_soxr_reset;
    r->impl.update_rates = resampler_soxr_update_rates;
    r->impl.resample = resampler_soxr_resample;
    r->impl.data = state;

    return 0;
}
