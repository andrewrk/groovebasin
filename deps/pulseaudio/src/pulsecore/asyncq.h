#ifndef foopulseasyncqhfoo
#define foopulseasyncqhfoo

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

#include <sys/types.h>
#include <pulse/def.h>
#include <pulsecore/macro.h>

/* A simple, asynchronous, lock-free (if requested also wait-free)
 * queue. Not multiple-reader/multiple-writer safe. If that is
 * required both sides can be protected by a mutex each. --- Which is
 * not a bad thing in most cases, since this queue is intended for
 * communication between a normal thread and a single real-time
 * thread. Only the real-time side needs to be lock-free/wait-free.
 *
 * If the queue is full and another entry shall be pushed, or when the
 * queue is empty and another entry shall be popped and the "wait"
 * argument is non-zero, the queue will block on a UNIX FIFO object --
 * that will probably require locking on the kernel side -- which
 * however is probably not problematic, because we do it only on
 * starvation or overload in which case we have to block anyway.  */

typedef struct pa_asyncq pa_asyncq;

pa_asyncq* pa_asyncq_new(unsigned size);
void pa_asyncq_free(pa_asyncq* q, pa_free_cb_t free_cb);

void* pa_asyncq_pop(pa_asyncq *q, bool wait);
int pa_asyncq_push(pa_asyncq *q, void *p, bool wait);

/* Similar to pa_asyncq_push(), but if the queue is full, postpone the
 * appending of the item locally and delay until
 * pa_asyncq_before_poll_post() is called. */
void pa_asyncq_post(pa_asyncq*l, void *p);

/* For the reading side */
int pa_asyncq_read_fd(pa_asyncq *q);
int pa_asyncq_read_before_poll(pa_asyncq *a);
void pa_asyncq_read_after_poll(pa_asyncq *a);

/* For the writing side */
int pa_asyncq_write_fd(pa_asyncq *q);
void pa_asyncq_write_before_poll(pa_asyncq *a);
void pa_asyncq_write_after_poll(pa_asyncq *a);

#endif
