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
#include <math.h>

#include <pulsecore/resampler.h>

struct peaks_data { /* data specific to the peak finder pseudo resampler */
    unsigned o_counter;
    unsigned i_counter;

    float max_f[PA_CHANNELS_MAX];
    int16_t max_i[PA_CHANNELS_MAX];
};

static unsigned peaks_resample(pa_resampler *r, const pa_memchunk *input, unsigned in_n_frames, pa_memchunk *output, unsigned *out_n_frames) {
    unsigned c, o_index = 0;
    unsigned i, i_end = 0;
    void *src, *dst;
    struct peaks_data *peaks_data;

    pa_assert(r);
    pa_assert(input);
    pa_assert(output);
    pa_assert(out_n_frames);

    peaks_data = r->impl.data;
    src = pa_memblock_acquire_chunk(input);
    dst = pa_memblock_acquire_chunk(output);

    i = ((uint64_t) peaks_data->o_counter * r->i_ss.rate) / r->o_ss.rate;
    i = i > peaks_data->i_counter ? i - peaks_data->i_counter : 0;

    while (i_end < in_n_frames) {
        i_end = ((uint64_t) (peaks_data->o_counter + 1) * r->i_ss.rate) / r->o_ss.rate;
        i_end = i_end > peaks_data->i_counter ? i_end - peaks_data->i_counter : 0;

        pa_assert_fp(o_index * r->w_fz < pa_memblock_get_length(output->memblock));

        /* 1ch float is treated separately, because that is the common case */
        if (r->work_channels == 1 && r->work_format == PA_SAMPLE_FLOAT32NE) {
            float *s = (float*) src + i;
            float *d = (float*) dst + o_index;

            for (; i < i_end && i < in_n_frames; i++) {
                float n = fabsf(*s++);

                if (n > peaks_data->max_f[0])
                    peaks_data->max_f[0] = n;
            }

            if (i == i_end) {
                *d = peaks_data->max_f[0];
                peaks_data->max_f[0] = 0;
                o_index++, peaks_data->o_counter++;
            }
        } else if (r->work_format == PA_SAMPLE_S16NE) {
            int16_t *s = (int16_t*) src + r->work_channels * i;
            int16_t *d = (int16_t*) dst + r->work_channels * o_index;

            for (; i < i_end && i < in_n_frames; i++)
                for (c = 0; c < r->work_channels; c++) {
                    int16_t n = abs(*s++);

                    if (n > peaks_data->max_i[c])
                        peaks_data->max_i[c] = n;
                }

            if (i == i_end) {
                for (c = 0; c < r->work_channels; c++, d++) {
                    *d = peaks_data->max_i[c];
                    peaks_data->max_i[c] = 0;
                }
                o_index++, peaks_data->o_counter++;
            }
        } else {
            float *s = (float*) src + r->work_channels * i;
            float *d = (float*) dst + r->work_channels * o_index;

            for (; i < i_end && i < in_n_frames; i++)
                for (c = 0; c < r->work_channels; c++) {
                    float n = fabsf(*s++);

                    if (n > peaks_data->max_f[c])
                        peaks_data->max_f[c] = n;
                }

            if (i == i_end) {
                for (c = 0; c < r->work_channels; c++, d++) {
                    *d = peaks_data->max_f[c];
                    peaks_data->max_f[c] = 0;
                }
                o_index++, peaks_data->o_counter++;
            }
        }
    }

    pa_memblock_release(input->memblock);
    pa_memblock_release(output->memblock);

    *out_n_frames = o_index;

    peaks_data->i_counter += in_n_frames;

    /* Normalize counters */
    while (peaks_data->i_counter >= r->i_ss.rate) {
        pa_assert(peaks_data->o_counter >= r->o_ss.rate);

        peaks_data->i_counter -= r->i_ss.rate;
        peaks_data->o_counter -= r->o_ss.rate;
    }

    return 0;
}

static void peaks_update_rates_or_reset(pa_resampler *r) {
    struct peaks_data *peaks_data;
    pa_assert(r);

    peaks_data = r->impl.data;

    peaks_data->i_counter = 0;
    peaks_data->o_counter = 0;
}

int pa_resampler_peaks_init(pa_resampler*r) {
    struct peaks_data *peaks_data;
    pa_assert(r);
    pa_assert(r->i_ss.rate >= r->o_ss.rate);
    pa_assert(r->work_format == PA_SAMPLE_S16NE || r->work_format == PA_SAMPLE_FLOAT32NE);

    peaks_data = pa_xnew0(struct peaks_data, 1);

    r->impl.resample = peaks_resample;
    r->impl.update_rates = peaks_update_rates_or_reset;
    r->impl.reset = peaks_update_rates_or_reset;
    r->impl.data = peaks_data;

    return 0;
}
