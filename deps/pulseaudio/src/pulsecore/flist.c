/***
  This file is part of PulseAudio.

  Copyright 2006-2008 Lennart Poettering
  Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
  Copyright (C) 2012 Canonical Ltd.

  Contact: Jyri Sarha <Jyri.Sarha@nokia.com>

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

#include <pulse/xmalloc.h>

#include <pulsecore/atomic.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

#include "flist.h"

#define FLIST_SIZE 256

/* Atomic table indices contain
   sign bit = if set, indicates empty/NULL value
   tag bits (to avoid the ABA problem)
   actual index bits
*/

/* Lock free single linked list element. */
struct pa_flist_elem {
    pa_atomic_t next;
    pa_atomic_ptr_t ptr;
};

typedef struct pa_flist_elem pa_flist_elem;

struct pa_flist {
    char *name;
    unsigned size;

    pa_atomic_t current_tag;
    int index_mask;
    int tag_shift;
    int tag_mask;

    /* Stack that contains pointers stored into free list */
    pa_atomic_t stored;
    /* Stack that contains empty list elements */
    pa_atomic_t empty;
    pa_flist_elem table[];
};

/* Lock free pop from linked list stack */
static pa_flist_elem *stack_pop(pa_flist *flist, pa_atomic_t *list) {
    pa_flist_elem *popped;
    int idx;
    pa_assert(list);

    do {
        idx = pa_atomic_load(list);
        if (idx < 0)
            return NULL;
        popped = &flist->table[idx & flist->index_mask];
    } while (!pa_atomic_cmpxchg(list, idx, pa_atomic_load(&popped->next)));

    return popped;
}

/* Lock free push to linked list stack */
static void stack_push(pa_flist *flist, pa_atomic_t *list, pa_flist_elem *new_elem) {
    int tag, newindex, next;
    pa_assert(list);

    tag = pa_atomic_inc(&flist->current_tag);
    newindex = new_elem - flist->table;
    pa_assert(newindex >= 0 && newindex < (int) flist->size);
    newindex |= (tag << flist->tag_shift) & flist->tag_mask;

    do {
        next = pa_atomic_load(list);
        pa_atomic_store(&new_elem->next, next);
    } while (!pa_atomic_cmpxchg(list, next, newindex));
}

pa_flist *pa_flist_new_with_name(unsigned size, const char *name) {
    pa_flist *l;
    unsigned i;
    pa_assert(name);

    if (!size)
        size = FLIST_SIZE;

    l = pa_xmalloc0(sizeof(pa_flist) + sizeof(pa_flist_elem) * size);

    l->name = pa_xstrdup(name);
    l->size = size;

    while (1 << l->tag_shift < (int) size)
        l->tag_shift++;
    l->index_mask = (1 << l->tag_shift) - 1;
    l->tag_mask = INT_MAX - l->index_mask;

    pa_atomic_store(&l->stored, -1);
    pa_atomic_store(&l->empty, -1);
    for (i=0; i < size; i++) {
        stack_push(l, &l->empty, &l->table[i]);
    }
    return l;
}

pa_flist *pa_flist_new(unsigned size) {
    return pa_flist_new_with_name(size, "unknown");
}

void pa_flist_free(pa_flist *l, pa_free_cb_t free_cb) {
    pa_assert(l);
    pa_assert(l->name);

    if (free_cb) {
        pa_flist_elem *elem;
        while((elem = stack_pop(l, &l->stored)))
            free_cb(pa_atomic_ptr_load(&elem->ptr));
    }

    pa_xfree(l->name);
    pa_xfree(l);
}

int pa_flist_push(pa_flist *l, void *p) {
    pa_flist_elem *elem;
    pa_assert(l);
    pa_assert(p);

    elem = stack_pop(l, &l->empty);
    if (elem == NULL) {
        if (pa_log_ratelimit(PA_LOG_DEBUG))
            pa_log_debug("%s flist is full (don't worry)", l->name);
        return -1;
    }
    pa_atomic_ptr_store(&elem->ptr, p);
    stack_push(l, &l->stored, elem);

    return 0;
}

void* pa_flist_pop(pa_flist *l) {
    pa_flist_elem *elem;
    void *ptr;
    pa_assert(l);

    elem = stack_pop(l, &l->stored);
    if (elem == NULL)
        return NULL;

    ptr = pa_atomic_ptr_load(&elem->ptr);

    stack_push(l, &l->empty, elem);

    return ptr;
}
