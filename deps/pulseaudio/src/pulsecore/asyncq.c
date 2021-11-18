/***
  This file is part of PulseAudio.

  Copyright 2006-2008 Lennart Poettering

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

#include <unistd.h>
#include <errno.h>

#include <pulse/xmalloc.h>

#include <pulsecore/atomic.h>
#include <pulsecore/log.h>
#include <pulsecore/thread.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/llist.h>
#include <pulsecore/flist.h>
#include <pulsecore/fdsem.h>

#include "asyncq.h"

#define ASYNCQ_SIZE 256

/* For debugging purposes we can define _Y to put an extra thread
 * yield between each operation. */

/* #define PROFILE */

#ifdef PROFILE
#define _Y pa_thread_yield()
#else
#define _Y do { } while(0)
#endif

struct localq {
    void *data;
    PA_LLIST_FIELDS(struct localq);
};

struct pa_asyncq {
    unsigned size;
    unsigned read_idx;
    unsigned write_idx;
    pa_fdsem *read_fdsem, *write_fdsem;

    PA_LLIST_HEAD(struct localq, localq);
    struct localq *last_localq;
    bool waiting_for_post;
};

PA_STATIC_FLIST_DECLARE(localq, 0, pa_xfree);

#define PA_ASYNCQ_CELLS(x) ((pa_atomic_ptr_t*) ((uint8_t*) (x) + PA_ALIGN(sizeof(struct pa_asyncq))))

static unsigned reduce(pa_asyncq *l, unsigned value) {
    return value & (unsigned) (l->size - 1);
}

pa_asyncq *pa_asyncq_new(unsigned size) {
    pa_asyncq *l;

    if (!size)
        size = ASYNCQ_SIZE;

    pa_assert(pa_is_power_of_two(size));

    l = pa_xmalloc0(PA_ALIGN(sizeof(pa_asyncq)) + (sizeof(pa_atomic_ptr_t) * size));

    l->size = size;

    PA_LLIST_HEAD_INIT(struct localq, l->localq);
    l->last_localq = NULL;
    l->waiting_for_post = false;

    if (!(l->read_fdsem = pa_fdsem_new())) {
        pa_xfree(l);
        return NULL;
    }

    if (!(l->write_fdsem = pa_fdsem_new())) {
        pa_fdsem_free(l->read_fdsem);
        pa_xfree(l);
        return NULL;
    }

    return l;
}

void pa_asyncq_free(pa_asyncq *l, pa_free_cb_t free_cb) {
    struct localq *q;
    pa_assert(l);

    if (free_cb) {
        void *p;

        while ((p = pa_asyncq_pop(l, 0)))
            free_cb(p);
    }

    while ((q = l->localq)) {
        if (free_cb)
            free_cb(q->data);

        PA_LLIST_REMOVE(struct localq, l->localq, q);

        if (pa_flist_push(PA_STATIC_FLIST_GET(localq), q) < 0)
            pa_xfree(q);
    }

    pa_fdsem_free(l->read_fdsem);
    pa_fdsem_free(l->write_fdsem);
    pa_xfree(l);
}

static int push(pa_asyncq*l, void *p, bool wait_op) {
    unsigned idx;
    pa_atomic_ptr_t *cells;

    pa_assert(l);
    pa_assert(p);

    cells = PA_ASYNCQ_CELLS(l);

    _Y;
    idx = reduce(l, l->write_idx);

    if (!pa_atomic_ptr_cmpxchg(&cells[idx], NULL, p)) {

        if (!wait_op)
            return -1;

/*         pa_log("sleeping on push"); */

        do {
            pa_fdsem_wait(l->read_fdsem);
        } while (!pa_atomic_ptr_cmpxchg(&cells[idx], NULL, p));
    }

    _Y;
    l->write_idx++;

    pa_fdsem_post(l->write_fdsem);

    return 0;
}

static bool flush_postq(pa_asyncq *l, bool wait_op) {
    struct localq *q;

    pa_assert(l);

    while ((q = l->last_localq)) {

        if (push(l, q->data, wait_op) < 0)
            return false;

        l->last_localq = q->prev;

        PA_LLIST_REMOVE(struct localq, l->localq, q);

        if (pa_flist_push(PA_STATIC_FLIST_GET(localq), q) < 0)
            pa_xfree(q);
    }

    return true;
}

int pa_asyncq_push(pa_asyncq*l, void *p, bool wait_op) {
    pa_assert(l);

    if (!flush_postq(l, wait_op))
        return -1;

    return push(l, p, wait_op);
}

void pa_asyncq_post(pa_asyncq*l, void *p) {
    struct localq *q;

    pa_assert(l);
    pa_assert(p);

    if (flush_postq(l, false))
        if (pa_asyncq_push(l, p, false) >= 0)
            return;

    /* OK, we couldn't push anything in the queue. So let's queue it
     * locally and push it later */

    if (pa_log_ratelimit(PA_LOG_WARN))
        pa_log_warn("q overrun, queuing locally");

    if (!(q = pa_flist_pop(PA_STATIC_FLIST_GET(localq))))
        q = pa_xnew(struct localq, 1);

    q->data = p;
    PA_LLIST_PREPEND(struct localq, l->localq, q);

    if (!l->last_localq)
        l->last_localq = q;

    return;
}

void* pa_asyncq_pop(pa_asyncq*l, bool wait_op) {
    unsigned idx;
    void *ret;
    pa_atomic_ptr_t *cells;

    pa_assert(l);

    cells = PA_ASYNCQ_CELLS(l);

    _Y;
    idx = reduce(l, l->read_idx);

    if (!(ret = pa_atomic_ptr_load(&cells[idx]))) {

        if (!wait_op)
            return NULL;

/*         pa_log("sleeping on pop"); */

        do {
            pa_fdsem_wait(l->write_fdsem);
        } while (!(ret = pa_atomic_ptr_load(&cells[idx])));
    }

    pa_assert(ret);

    /* Guaranteed to succeed if we only have a single reader */
    pa_assert_se(pa_atomic_ptr_cmpxchg(&cells[idx], ret, NULL));

    _Y;
    l->read_idx++;

    pa_fdsem_post(l->read_fdsem);

    return ret;
}

int pa_asyncq_read_fd(pa_asyncq *q) {
    pa_assert(q);

    return pa_fdsem_get(q->write_fdsem);
}

int pa_asyncq_read_before_poll(pa_asyncq *l) {
    unsigned idx;
    pa_atomic_ptr_t *cells;

    pa_assert(l);

    cells = PA_ASYNCQ_CELLS(l);

    _Y;
    idx = reduce(l, l->read_idx);

    for (;;) {
        if (pa_atomic_ptr_load(&cells[idx]))
            return -1;

        if (pa_fdsem_before_poll(l->write_fdsem) >= 0)
            return 0;
    }
}

void pa_asyncq_read_after_poll(pa_asyncq *l) {
    pa_assert(l);

    pa_fdsem_after_poll(l->write_fdsem);
}

int pa_asyncq_write_fd(pa_asyncq *q) {
    pa_assert(q);

    return pa_fdsem_get(q->read_fdsem);
}

void pa_asyncq_write_before_poll(pa_asyncq *l) {
    pa_assert(l);

    for (;;) {

        if (flush_postq(l, false))
            break;

        if (pa_fdsem_before_poll(l->read_fdsem) >= 0) {
            l->waiting_for_post = true;
            break;
        }
    }
}

void pa_asyncq_write_after_poll(pa_asyncq *l) {
    pa_assert(l);

    if (l->waiting_for_post) {
        pa_fdsem_after_poll(l->read_fdsem);
        l->waiting_for_post = false;
    }
}
