#ifndef foolfefilterhfoo
#define foolfefilterhfoo

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
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulsecore/memchunk.h>
#include <pulsecore/memblockq.h>

typedef struct pa_lfe_filter pa_lfe_filter_t;

pa_lfe_filter_t * pa_lfe_filter_new(const pa_sample_spec* ss, const pa_channel_map* cm, float crossover_freq, size_t maxrewind);
void pa_lfe_filter_free(pa_lfe_filter_t *);
void pa_lfe_filter_reset(pa_lfe_filter_t *);
void pa_lfe_filter_rewind(pa_lfe_filter_t *, size_t amount);
pa_memchunk * pa_lfe_filter_process(pa_lfe_filter_t *filter, pa_memchunk *buf);
void pa_lfe_filter_update_rate(pa_lfe_filter_t *, uint32_t new_rate);

#endif
