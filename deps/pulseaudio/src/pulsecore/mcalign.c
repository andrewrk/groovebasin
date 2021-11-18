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

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>

#include "mcalign.h"

struct pa_mcalign {
    size_t base;
    pa_memchunk leftover, current;
};

pa_mcalign *pa_mcalign_new(size_t base) {
    pa_mcalign *m;
    pa_assert(base);

    m = pa_xnew(pa_mcalign, 1);

    m->base = base;
    pa_memchunk_reset(&m->leftover);
    pa_memchunk_reset(&m->current);

    return m;
}

void pa_mcalign_free(pa_mcalign *m) {
    pa_assert(m);

    if (m->leftover.memblock)
        pa_memblock_unref(m->leftover.memblock);

    if (m->current.memblock)
        pa_memblock_unref(m->current.memblock);

    pa_xfree(m);
}

void pa_mcalign_push(pa_mcalign *m, const pa_memchunk *c) {
    pa_assert(m);
    pa_assert(c);

    pa_assert(c->memblock);
    pa_assert(c->length > 0);

    pa_assert(!m->current.memblock);

    /* Append to the leftover memory block */
    if (m->leftover.memblock) {

        /* Try to merge */
        if (m->leftover.memblock == c->memblock &&
            m->leftover.index + m->leftover.length == c->index) {

            /* Merge */
            m->leftover.length += c->length;

            /* If the new chunk is larger than m->base, move it to current */
            if (m->leftover.length >= m->base) {
                m->current = m->leftover;
                pa_memchunk_reset(&m->leftover);
            }

        } else {
            size_t l;
            void *lo_data, *m_data;

            /* We have to copy */
            pa_assert(m->leftover.length < m->base);
            l = m->base - m->leftover.length;

            if (l > c->length)
                l = c->length;

            /* Can we use the current block? */
            pa_memchunk_make_writable(&m->leftover, m->base);

            lo_data = pa_memblock_acquire(m->leftover.memblock);
            m_data = pa_memblock_acquire(c->memblock);
            memcpy((uint8_t*) lo_data + m->leftover.index + m->leftover.length, (uint8_t*) m_data + c->index, l);
            pa_memblock_release(m->leftover.memblock);
            pa_memblock_release(c->memblock);
            m->leftover.length += l;

            pa_assert(m->leftover.length <= m->base);
            pa_assert(m->leftover.length <= pa_memblock_get_length(m->leftover.memblock));

            if (c->length > l) {
                /* Save the remainder of the memory block */
                m->current = *c;
                m->current.index += l;
                m->current.length -= l;
                pa_memblock_ref(m->current.memblock);
            }
        }
    } else {
        /* Nothing to merge or copy, just store it */

        if (c->length >= m->base)
            m->current = *c;
        else
            m->leftover = *c;

        pa_memblock_ref(c->memblock);
    }
}

int pa_mcalign_pop(pa_mcalign *m, pa_memchunk *c) {
    pa_assert(m);
    pa_assert(c);

    /* First test if there's a leftover memory block available */
    if (m->leftover.memblock) {
        pa_assert(m->leftover.length > 0);
        pa_assert(m->leftover.length <= m->base);

        /* The leftover memory block is not yet complete */
        if (m->leftover.length < m->base)
            return -1;

        /* Return the leftover memory block */
        *c = m->leftover;
        pa_memchunk_reset(&m->leftover);

        /* If the current memblock is too small move it the leftover */
        if (m->current.memblock && m->current.length < m->base) {
            m->leftover = m->current;
            pa_memchunk_reset(&m->current);
        }

        return 0;
    }

    /* Now let's see if there is other data available */
    if (m->current.memblock) {
        size_t l;
        pa_assert(m->current.length >= m->base);

        /* The length of the returned memory block */
        l = m->current.length;
        l /= m->base;
        l *= m->base;
        pa_assert(l > 0);

        /* Prepare the returned block */
        *c = m->current;
        pa_memblock_ref(c->memblock);
        c->length = l;

        /* Drop that from the current memory block */
        pa_assert(l <= m->current.length);
        m->current.index += l;
        m->current.length -= l;

        /* In case the whole block was dropped ... */
        if (m->current.length == 0)
            pa_memblock_unref(m->current.memblock);
        else {
            /* Move the remainder to leftover */
            pa_assert(m->current.length < m->base && !m->leftover.memblock);

            m->leftover = m->current;
        }

        pa_memchunk_reset(&m->current);

        return 0;
    }

    /* There's simply nothing */
    return -1;
}

size_t pa_mcalign_csize(pa_mcalign *m, size_t l) {
    pa_assert(m);
    pa_assert(l > 0);

    pa_assert(!m->current.memblock);

    if (m->leftover.memblock)
        l += m->leftover.length;

    return (l/m->base)*m->base;
}

void pa_mcalign_flush(pa_mcalign *m) {
    pa_memchunk chunk;
    pa_assert(m);

    while (pa_mcalign_pop(m, &chunk) >= 0)
        pa_memblock_unref(chunk.memblock);
}
