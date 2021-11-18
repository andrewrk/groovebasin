#ifndef foomemblockqhfoo
#define foomemblockqhfoo

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
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#include <sys/types.h>
#include <inttypes.h>

#include <pulsecore/memblock.h>
#include <pulsecore/memchunk.h>
#include <pulse/def.h>

/* A memblockq is a queue of pa_memchunks (yep, the name is not
 * perfect). It is similar to the ring buffers used by most other
 * audio software. In contrast to a ring buffer this memblockq data
 * type doesn't need to copy any data around, it just maintains
 * references to reference counted memory blocks. */

typedef struct pa_memblockq pa_memblockq;

/* Parameters:

   - name:      name for debugging purposes

   - idx:       start value for both read and write index

   - maxlength: maximum length of queue. If more data is pushed into
                the queue, the operation will fail. Must not be 0.

   - tlength:   the target length of the queue. Pass 0 for the default.

   - ss:        Sample spec describing the queue contents. Only multiples
                of the frame size as implied by the sample spec are
                allowed into the queue or can be popped from it.

   - prebuf:    If the queue runs empty wait until this many bytes are in
                queue again before passing the first byte out. If set
                to 0 pa_memblockq_pop() will return a silence memblock
                if no data is in the queue and will never fail. Pass
                (size_t) -1 for the default.

   - minreq:    pa_memblockq_pop_missing() will only return values greater
                than this value. Pass 0 for the default.

   - maxrewind: how many bytes of history to keep in the queue

   - silence:   return this memchunk when reading uninitialized data
*/
pa_memblockq* pa_memblockq_new(
        const char *name,
        int64_t idx,
        size_t maxlength,
        size_t tlength,
        const pa_sample_spec *sample_spec,
        size_t prebuf,
        size_t minreq,
        size_t maxrewind,
        pa_memchunk *silence);

void pa_memblockq_free(pa_memblockq*bq);

/* Push a new memory chunk into the queue.  */
int pa_memblockq_push(pa_memblockq* bq, const pa_memchunk *chunk);

/* Push a new memory chunk into the queue, but filter it through a
 * pa_mcalign object. Don't mix this with pa_memblockq_seek() unless
 * you know what you do. */
int pa_memblockq_push_align(pa_memblockq* bq, const pa_memchunk *chunk);

/* Manipulate the write pointer */
void pa_memblockq_seek(pa_memblockq *bq, int64_t offset, pa_seek_mode_t seek, bool account);

/* Return a copy of the next memory chunk in the queue. It is not
 * removed from the queue. There are two reasons this function might
 * fail: 1. prebuffering is active, 2. queue is empty and no silence
 * memblock was passed at initialization. If the queue is not empty,
 * but we're currently at a hole in the queue and no silence memblock
 * was passed we return the length of the hole in chunk->length. */
int pa_memblockq_peek(pa_memblockq* bq, pa_memchunk *chunk);

/* Much like pa_memblockq_peek, but guarantees that the returned chunk
 * will have a length of the block size passed. You must configure a
 * silence memchunk for this memblockq if you use this call. */
int pa_memblockq_peek_fixed_size(pa_memblockq *bq, size_t block_size, pa_memchunk *chunk);

/* Drop the specified bytes from the queue. */
void pa_memblockq_drop(pa_memblockq *bq, size_t length);

/* Rewind the read index. If the history is shorter than the specified length we'll point to silence afterwards. */
void pa_memblockq_rewind(pa_memblockq *bq, size_t length);

/* Test if the pa_memblockq is currently readable, that is, more data than base */
bool pa_memblockq_is_readable(pa_memblockq *bq);

/* Return the length of the queue in bytes */
size_t pa_memblockq_get_length(pa_memblockq *bq);

/* Return the number of bytes that are missing since the last call to
 * this function, reset the internal counter to 0. */
size_t pa_memblockq_pop_missing(pa_memblockq *bq);

/* Directly moves the data from the source memblockq into bq */
int pa_memblockq_splice(pa_memblockq *bq, pa_memblockq *source);

/* Set the queue to silence, set write index to read index */
void pa_memblockq_flush_write(pa_memblockq *bq, bool account);

/* Set the queue to silence, set write read index to write index*/
void pa_memblockq_flush_read(pa_memblockq *bq);

/* Ignore prebuf for now */
void pa_memblockq_prebuf_disable(pa_memblockq *bq);

/* Force prebuf */
void pa_memblockq_prebuf_force(pa_memblockq *bq);

/* Return the maximum length of the queue in bytes */
size_t pa_memblockq_get_maxlength(pa_memblockq *bq);

/* Get Target length */
size_t pa_memblockq_get_tlength(pa_memblockq *bq);

/* Return the prebuffer length in bytes */
size_t pa_memblockq_get_prebuf(pa_memblockq *bq);

/* Returns the minimal request value */
size_t pa_memblockq_get_minreq(pa_memblockq *bq);

/* Returns the maximal rewind value */
size_t pa_memblockq_get_maxrewind(pa_memblockq *bq);

/* Return the base unit in bytes */
size_t pa_memblockq_get_base(pa_memblockq *bq);

/* Return the current read index */
int64_t pa_memblockq_get_read_index(pa_memblockq *bq);

/* Return the current write index */
int64_t pa_memblockq_get_write_index(pa_memblockq *bq);

/* Change metrics. Always call in order. */
void pa_memblockq_set_maxlength(pa_memblockq *memblockq, size_t maxlength); /* might modify tlength, prebuf, minreq too */
void pa_memblockq_set_tlength(pa_memblockq *memblockq, size_t tlength); /* might modify minreq, too */
void pa_memblockq_set_minreq(pa_memblockq *memblockq, size_t minreq); /* might modify prebuf, too */
void pa_memblockq_set_prebuf(pa_memblockq *memblockq, size_t prebuf);
void pa_memblockq_set_maxrewind(pa_memblockq *memblockq, size_t maxrewind); /* Set the maximum history size */
void pa_memblockq_set_silence(pa_memblockq *memblockq, pa_memchunk *silence);

/* Apply the data from pa_buffer_attr */
void pa_memblockq_apply_attr(pa_memblockq *memblockq, const pa_buffer_attr *a);
void pa_memblockq_get_attr(pa_memblockq *bq, pa_buffer_attr *a);

/* Call pa_memchunk_will_need() for every chunk in the queue from the current read pointer to the end */
void pa_memblockq_willneed(pa_memblockq *bq);

/* Check whether the memblockq is completely empty, i.e. no data
 * neither left nor right of the read pointer, and hence no buffered
 * data for the future nor data in the backlog. */
bool pa_memblockq_is_empty(pa_memblockq *bq);

/* Drop everything in the queue, but don't modify the indexes */
void pa_memblockq_silence(pa_memblockq *bq);

/* Check whether we currently are in prebuf state */
bool pa_memblockq_prebuf_active(pa_memblockq *bq);

/* Return how many items are currently stored in the queue */
unsigned pa_memblockq_get_nblocks(pa_memblockq *bq);

#endif
