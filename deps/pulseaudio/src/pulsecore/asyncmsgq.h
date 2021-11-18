#ifndef foopulseasyncmsgqhfoo
#define foopulseasyncmsgqhfoo

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

#include <pulsecore/asyncq.h>
#include <pulsecore/memchunk.h>
#include <pulsecore/msgobject.h>

/* A simple asynchronous message queue, based on pa_asyncq. In
 * contrast to pa_asyncq this one is multiple-writer safe, though
 * still not multiple-reader safe. This queue is intended to be used
 * for controlling real-time threads from normal-priority
 * threads. Multiple-writer-safety is accomplished by using a mutex on
 * the writer side. This queue is thus not useful for communication
 * between several real-time threads.
 *
 * The queue takes messages consisting of:
 *    "Object" for which this messages is intended (may be NULL)
 *    A numeric message code
 *    Arbitrary userdata pointer (may be NULL)
 *    A memchunk (may be NULL)
 *
 * There are two functions for submitting messages: _post and
 * _send. The former just enqueues the message asynchronously, the
 * latter waits for completion, synchronously. */

enum {
    PA_MESSAGE_SHUTDOWN = -1/* A generic message to inform the handler of this queue to quit */
};

typedef struct pa_asyncmsgq pa_asyncmsgq;

pa_asyncmsgq* pa_asyncmsgq_new(unsigned size);
pa_asyncmsgq* pa_asyncmsgq_ref(pa_asyncmsgq *q);

void pa_asyncmsgq_unref(pa_asyncmsgq* q);

void pa_asyncmsgq_post(pa_asyncmsgq *q, pa_msgobject *object, int code, const void *userdata, int64_t offset, const pa_memchunk *memchunk, pa_free_cb_t userdata_free_cb);
int pa_asyncmsgq_send(pa_asyncmsgq *q, pa_msgobject *object, int code, const void *userdata, int64_t offset, const pa_memchunk *memchunk);

int pa_asyncmsgq_get(pa_asyncmsgq *q, pa_msgobject **object, int *code, void **userdata, int64_t *offset, pa_memchunk *memchunk, bool wait);
int pa_asyncmsgq_dispatch(pa_msgobject *object, int code, void *userdata, int64_t offset, pa_memchunk *memchunk);
void pa_asyncmsgq_done(pa_asyncmsgq *q, int ret);
int pa_asyncmsgq_wait_for(pa_asyncmsgq *a, int code);
int pa_asyncmsgq_process_one(pa_asyncmsgq *a);

void pa_asyncmsgq_flush(pa_asyncmsgq *a, bool run);

/* For the reading side */
int pa_asyncmsgq_read_fd(pa_asyncmsgq *q);
int pa_asyncmsgq_read_before_poll(pa_asyncmsgq *a);
void pa_asyncmsgq_read_after_poll(pa_asyncmsgq *a);

/* For the write side */
int pa_asyncmsgq_write_fd(pa_asyncmsgq *q);
void pa_asyncmsgq_write_before_poll(pa_asyncmsgq *a);
void pa_asyncmsgq_write_after_poll(pa_asyncmsgq *a);

bool pa_asyncmsgq_dispatching(pa_asyncmsgq *a);

#endif
