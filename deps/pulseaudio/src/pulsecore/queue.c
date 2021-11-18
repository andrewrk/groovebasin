/***
  This file is part of PulseAudio.

  Copyright 2004-2008 Lennart Poettering

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

#include <stdlib.h>

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>
#include <pulsecore/flist.h>

#include "queue.h"

PA_STATIC_FLIST_DECLARE(entries, 0, pa_xfree);

struct queue_entry {
    struct queue_entry *next;
    void *data;
};

struct pa_queue {
    struct queue_entry *front, *back;
    unsigned length;
};

pa_queue* pa_queue_new(void) {
    pa_queue *q = pa_xnew(pa_queue, 1);

    q->front = q->back = NULL;
    q->length = 0;

    return q;
}

void pa_queue_free(pa_queue *q, pa_free_cb_t free_func) {
    void *data;
    pa_assert(q);

    while ((data = pa_queue_pop(q)))
        if (free_func)
            free_func(data);

    pa_assert(!q->front);
    pa_assert(!q->back);
    pa_assert(q->length == 0);

    pa_xfree(q);
}

void pa_queue_push(pa_queue *q, void *p) {
    struct queue_entry *e;

    pa_assert(q);
    pa_assert(p);

    if (!(e = pa_flist_pop(PA_STATIC_FLIST_GET(entries))))
        e = pa_xnew(struct queue_entry, 1);

    e->data = p;
    e->next = NULL;

    if (q->back) {
        pa_assert(q->front);
        q->back->next = e;
    } else {
        pa_assert(!q->front);
        q->front = e;
    }

    q->back = e;
    q->length++;
}

void* pa_queue_pop(pa_queue *q) {
    void *p;
    struct queue_entry *e;

    pa_assert(q);

    if (!(e = q->front))
        return NULL;

    q->front = e->next;

    if (q->back == e) {
        pa_assert(!e->next);
        q->back = NULL;
    }

    p = e->data;

    if (pa_flist_push(PA_STATIC_FLIST_GET(entries), e) < 0)
        pa_xfree(e);

    q->length--;

    return p;
}

int pa_queue_isempty(pa_queue *q) {
    pa_assert(q);

    return q->length == 0;
}
