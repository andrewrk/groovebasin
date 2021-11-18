#ifndef foomemchunkhfoo
#define foomemchunkhfoo

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

typedef struct pa_memchunk pa_memchunk;

#include <pulsecore/memblock.h>

/* A memchunk describes a part of a memblock. In contrast to the memblock, a
 * memchunk is not allocated dynamically or reference counted, instead
 * it is usually stored on the stack and copied around */

struct pa_memchunk {
    pa_memblock *memblock;
    size_t index, length;
};

/* Make a memchunk writable, i.e. make sure that the caller may have
 * exclusive access to the memblock and it is not read-only. If needed
 * the memblock in the structure is replaced by a copy. If min is not
 * 0 it is made sure that the returned memblock is at least of the
 * specified size, i.e. is enlarged if necessary. */
pa_memchunk* pa_memchunk_make_writable(pa_memchunk *c, size_t min);

/* Invalidate a memchunk. This does not free the containing memblock,
 * but sets all members to zero. */
pa_memchunk* pa_memchunk_reset(pa_memchunk *c);

/* Map a memory chunk back into memory if it was swapped out */
pa_memchunk *pa_memchunk_will_need(const pa_memchunk *c);

/* Copy the data in the src memchunk to the dst memchunk */
pa_memchunk* pa_memchunk_memcpy(pa_memchunk *dst, pa_memchunk *src);

/* Return true if any field is set != 0 */
bool pa_memchunk_isset(pa_memchunk *c);

#endif
