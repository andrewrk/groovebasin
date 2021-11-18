/***
  This file is part of PulseAudio.

  Copyright 2014 David Henningsson, Canonical Ltd.

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

#include "lfe-filter.h"
#include <pulse/xmalloc.h>
#include <pulsecore/flist.h>
#include <pulsecore/llist.h>
#include <pulsecore/filter/biquad.h>
#include <pulsecore/filter/crossover.h>

struct saved_state {
    PA_LLIST_FIELDS(struct saved_state);
    pa_memchunk chunk;
    int64_t index;
    struct lr4 lr4[PA_CHANNELS_MAX];
};

PA_STATIC_FLIST_DECLARE(lfe_state, 0, pa_xfree);

/* An LR4 filter, implemented as a chain of two Butterworth filters.

   Currently the channel map is fixed so that a highpass filter is applied to all
   channels except for the LFE channel, where a lowpass filter is applied.
   This works well for e g stereo to 2.1/5.1/7.1 scenarios, where the remap engine
   has calculated the LFE channel to be the average of all source channels.
*/

struct pa_lfe_filter {
    int64_t index;
    PA_LLIST_HEAD(struct saved_state, saved);
    float crossover;
    pa_channel_map cm;
    pa_sample_spec ss;
    size_t maxrewind;
    bool active;
    struct lr4 lr4[PA_CHANNELS_MAX];
};

static void remove_state(pa_lfe_filter_t *f, struct saved_state *s) {
    PA_LLIST_REMOVE(struct saved_state, f->saved, s);
    pa_memblock_unref(s->chunk.memblock);
    pa_xfree(s);
}

pa_lfe_filter_t * pa_lfe_filter_new(const pa_sample_spec* ss, const pa_channel_map* cm, float crossover_freq, size_t maxrewind) {

    pa_lfe_filter_t *f = pa_xnew0(struct pa_lfe_filter, 1);
    f->crossover = crossover_freq;
    f->cm = *cm;
    f->ss = *ss;
    f->maxrewind = maxrewind;
    pa_lfe_filter_update_rate(f, ss->rate);
    return f;
}

void pa_lfe_filter_free(pa_lfe_filter_t *f) {
    while (f->saved)
        remove_state(f, f->saved);

    pa_xfree(f);
}

void pa_lfe_filter_reset(pa_lfe_filter_t *f) {
    pa_lfe_filter_update_rate(f, f->ss.rate);
}

static void process_block(pa_lfe_filter_t *f, pa_memchunk *buf, bool store_result) {
    int samples = buf->length / pa_frame_size(&f->ss);

    void *garbage = store_result ? NULL : pa_xmalloc(buf->length);

    if (f->ss.format == PA_SAMPLE_FLOAT32NE) {
        int i;
        float *data = pa_memblock_acquire_chunk(buf);
        for (i = 0; i < f->cm.channels; i++)
            lr4_process_float32(&f->lr4[i], samples, f->cm.channels, &data[i], garbage ? garbage : &data[i]);
        pa_memblock_release(buf->memblock);
    }
    else if (f->ss.format == PA_SAMPLE_S16NE) {
        int i;
        short *data = pa_memblock_acquire_chunk(buf);
        for (i = 0; i < f->cm.channels; i++)
            lr4_process_s16(&f->lr4[i], samples, f->cm.channels, &data[i], garbage ? garbage : &data[i]);
        pa_memblock_release(buf->memblock);
    }
    else pa_assert_not_reached();

    pa_xfree(garbage);
    f->index += samples;
}

pa_memchunk * pa_lfe_filter_process(pa_lfe_filter_t *f, pa_memchunk *buf) {
    pa_mempool *pool;
    struct saved_state *s, *s2;
    void *data;

    if (!f->active || !buf->length)
        return buf;

    /* Remove old states (FIXME: we could do better than searching the entire array here?) */
    PA_LLIST_FOREACH_SAFE(s, s2, f->saved)
        if (s->index + (int64_t) (s->chunk.length / pa_frame_size(&f->ss) + f->maxrewind) < f->index)
            remove_state(f, s);

    /* Insert our existing state into the flist */
    if ((s = pa_flist_pop(PA_STATIC_FLIST_GET(lfe_state))) == NULL)
        s = pa_xnew(struct saved_state, 1);
    PA_LLIST_INIT(struct saved_state, s);

    /* TODO: This actually memcpys the entire chunk into a new allocation, because we need to retain the original
       in case of rewinding. Investigate whether this can be avoided. */
    data = pa_memblock_acquire_chunk(buf);
    pool = pa_memblock_get_pool(buf->memblock);
    s->chunk.memblock = pa_memblock_new_malloced(pool, pa_xmemdup(data, buf->length), buf->length);
    s->chunk.length = buf->length;
    s->chunk.index = 0;
    pa_memblock_release(buf->memblock);
    pa_mempool_unref(pool), pool = NULL;

    s->index = f->index;
    memcpy(s->lr4, f->lr4, sizeof(struct lr4) * f->cm.channels);
    PA_LLIST_PREPEND(struct saved_state, f->saved, s);

    process_block(f, buf, true);
    return buf;
}

void pa_lfe_filter_update_rate(pa_lfe_filter_t *f, uint32_t new_rate) {
    int i;
    float biquad_freq = f->crossover / (new_rate / 2);

    while (f->saved)
        remove_state(f, f->saved);

    f->index = 0;
    f->ss.rate = new_rate;
    if (biquad_freq <= 0 || biquad_freq >= 1) {
        pa_log_warn("Crossover frequency (%f) outside range for sample rate %d", f->crossover, new_rate);
        f->active = false;
        return;
    }

    for (i = 0; i < f->cm.channels; i++)
        lr4_set(&f->lr4[i], f->cm.map[i] == PA_CHANNEL_POSITION_LFE ? BQ_LOWPASS : BQ_HIGHPASS, biquad_freq);

    f->active = true;
}

void pa_lfe_filter_rewind(pa_lfe_filter_t *f, size_t amount) {
    struct saved_state *i, *s = NULL;
    size_t samples = amount / pa_frame_size(&f->ss);
    f->index -= samples;

    /* Find the closest saved position */
    PA_LLIST_FOREACH(i, f->saved) {
        if (i->index > f->index)
            continue;
        if (s == NULL || i->index > s->index)
            s = i;
    }
    if (s == NULL) {
        pa_log_debug("Rewinding LFE filter %zu samples to position %lli. No saved state found", samples, (long long) f->index);
        pa_lfe_filter_update_rate(f, f->ss.rate);
        return;
    }
    pa_log_debug("Rewinding LFE filter %zu samples to position %lli. Found saved state at position %lli",
        samples, (long long) f->index, (long long) s->index);
    memcpy(f->lr4, s->lr4, sizeof(struct lr4) * f->cm.channels);

    /* now fast forward to the actual position */
    if (f->index > s->index) {
        pa_memchunk x = s->chunk;
        x.length = (f->index - s->index) * pa_frame_size(&f->ss);
        if (x.length > s->chunk.length) {
            pa_log_error("Hole in stream, cannot fast forward LFE filter");
            return;
        }
        f->index = s->index;
        process_block(f, &x, false);
    }
}
