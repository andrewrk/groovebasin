/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>

#include "memchunk.h"

pa_memchunk* pa_memchunk_make_writable(pa_memchunk *c, size_t min) {
    pa_mempool *pool;
    pa_memblock *n;
    size_t l;
    void *tdata, *sdata;

    pa_assert(c);
    pa_assert(c->memblock);

    if (pa_memblock_ref_is_one(c->memblock) &&
        !pa_memblock_is_read_only(c->memblock) &&
        pa_memblock_get_length(c->memblock) >= c->index+min)
        return c;

    l = PA_MAX(c->length, min);

    pool = pa_memblock_get_pool(c->memblock);
    n = pa_memblock_new(pool, l);
    pa_mempool_unref(pool), pool = NULL;

    sdata = pa_memblock_acquire(c->memblock);
    tdata = pa_memblock_acquire(n);

    memcpy(tdata, (uint8_t*) sdata + c->index, c->length);

    pa_memblock_release(c->memblock);
    pa_memblock_release(n);

    pa_memblock_unref(c->memblock);

    c->memblock = n;
    c->index = 0;

    return c;
}

pa_memchunk* pa_memchunk_reset(pa_memchunk *c) {
    pa_assert(c);

    memset(c, 0, sizeof(*c));

    return c;
}

pa_memchunk *pa_memchunk_will_need(const pa_memchunk *c) {
    void *p;

    pa_assert(c);
    pa_assert(c->memblock);

    /* A version of pa_memblock_will_need() that works on memchunks
     * instead of memblocks */

    p = pa_memblock_acquire_chunk(c);
    pa_will_need(p, c->length);
    pa_memblock_release(c->memblock);

    return (pa_memchunk*) c;
}

pa_memchunk* pa_memchunk_memcpy(pa_memchunk *dst, pa_memchunk *src) {
    void *p, *q;

    pa_assert(dst);
    pa_assert(src);
    pa_assert(dst->length == src->length);

    p = pa_memblock_acquire(dst->memblock);
    q = pa_memblock_acquire(src->memblock);

    memmove((uint8_t*) p + dst->index,
            (uint8_t*) q + src->index,
            dst->length);

    pa_memblock_release(dst->memblock);
    pa_memblock_release(src->memblock);

    return dst;
}

bool pa_memchunk_isset(pa_memchunk *chunk) {
    pa_assert(chunk);

    return
        chunk->memblock ||
        chunk->index > 0 ||
        chunk->length > 0;
}
