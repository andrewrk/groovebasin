#ifndef foomcalignhfoo
#define foomcalignhfoo

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

#include <pulsecore/memblock.h>
#include <pulsecore/memchunk.h>

/* An alignment object, used for aligning memchunks to multiples of
 * the frame size. */

/* Method of operation: the user creates a new mcalign object by
 * calling pa_mcalign_new() with the appropriate aligning
 * granularity. After that they may call pa_mcalign_push() for an input
 * memchunk. After exactly one memchunk the user has to call
 * pa_mcalign_pop() until it returns -1. If pa_mcalign_pop() returns
 * 0, the memchunk *c is valid and aligned to the granularity. Some
 * pseudocode illustrating this:
 *
 * pa_mcalign *a = pa_mcalign_new(4, NULL);
 *
 * for (;;) {
 *   pa_memchunk input;
 *
 *   ... fill input ...
 *
 *   pa_mcalign_push(m, &input);
 *   pa_memblock_unref(input.memblock);
 *
 *   for (;;) {
 *     pa_memchunk output;
 *
 *     if (pa_mcalign_pop(m, &output) < 0)
 *       break;
 *
 *     ... consume output ...
 *
 *     pa_memblock_unref(output.memblock);
 *   }
 * }
 *
 * pa_memchunk_free(a);
 * */

typedef struct pa_mcalign pa_mcalign;

pa_mcalign *pa_mcalign_new(size_t base);
void pa_mcalign_free(pa_mcalign *m);

/* Push a new memchunk into the aligner. The caller of this routine
 * has to free the memchunk by himself. */
void pa_mcalign_push(pa_mcalign *m, const pa_memchunk *c);

/* Pop a new memchunk from the aligner. Returns 0 when successful,
 * nonzero otherwise. */
int pa_mcalign_pop(pa_mcalign *m, pa_memchunk *c);

/* If we pass l bytes in now, how many bytes would we get out? */
size_t pa_mcalign_csize(pa_mcalign *m, size_t l);

/* Flush what's still stored in the aligner */
void pa_mcalign_flush(pa_mcalign *m);

#endif
