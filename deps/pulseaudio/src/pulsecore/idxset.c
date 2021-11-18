/***
  This file is part of PulseAudio.

  Copyright 2004-2008 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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
#include <pulsecore/flist.h>
#include <pulsecore/macro.h>

#include "idxset.h"

#define NBUCKETS 127

struct idxset_entry {
    uint32_t idx;
    void *data;

    struct idxset_entry *data_next, *data_previous;
    struct idxset_entry *index_next, *index_previous;
    struct idxset_entry *iterate_next, *iterate_previous;
};

struct pa_idxset {
    pa_hash_func_t hash_func;
    pa_compare_func_t compare_func;

    uint32_t current_index;

    struct idxset_entry *iterate_list_head, *iterate_list_tail;
    unsigned n_entries;
};

#define BY_DATA(i) ((struct idxset_entry**) ((uint8_t*) (i) + PA_ALIGN(sizeof(pa_idxset))))
#define BY_INDEX(i) (BY_DATA(i) + NBUCKETS)

PA_STATIC_FLIST_DECLARE(entries, 0, pa_xfree);

unsigned pa_idxset_string_hash_func(const void *p) {
    unsigned hash = 0;
    const char *c;

    for (c = p; *c; c++)
        hash = 31 * hash + (unsigned) *c;

    return hash;
}

int pa_idxset_string_compare_func(const void *a, const void *b) {
    return strcmp(a, b);
}

unsigned pa_idxset_trivial_hash_func(const void *p) {
    return PA_PTR_TO_UINT(p);
}

int pa_idxset_trivial_compare_func(const void *a, const void *b) {
    return a < b ? -1 : (a > b ? 1 : 0);
}

pa_idxset* pa_idxset_new(pa_hash_func_t hash_func, pa_compare_func_t compare_func) {
    pa_idxset *s;

    s = pa_xmalloc0(PA_ALIGN(sizeof(pa_idxset)) + NBUCKETS*2*sizeof(struct idxset_entry*));

    s->hash_func = hash_func ? hash_func : pa_idxset_trivial_hash_func;
    s->compare_func = compare_func ? compare_func : pa_idxset_trivial_compare_func;

    s->current_index = 0;
    s->n_entries = 0;
    s->iterate_list_head = s->iterate_list_tail = NULL;

    return s;
}

static void remove_entry(pa_idxset *s, struct idxset_entry *e) {
    pa_assert(s);
    pa_assert(e);

    /* Remove from iteration linked list */
    if (e->iterate_next)
        e->iterate_next->iterate_previous = e->iterate_previous;
    else
        s->iterate_list_tail = e->iterate_previous;

    if (e->iterate_previous)
        e->iterate_previous->iterate_next = e->iterate_next;
    else
        s->iterate_list_head = e->iterate_next;

    /* Remove from data hash table */
    if (e->data_next)
        e->data_next->data_previous = e->data_previous;

    if (e->data_previous)
        e->data_previous->data_next = e->data_next;
    else {
        unsigned hash = s->hash_func(e->data) % NBUCKETS;
        BY_DATA(s)[hash] = e->data_next;
    }

    /* Remove from index hash table */
    if (e->index_next)
        e->index_next->index_previous = e->index_previous;

    if (e->index_previous)
        e->index_previous->index_next = e->index_next;
    else
        BY_INDEX(s)[e->idx % NBUCKETS] = e->index_next;

    if (pa_flist_push(PA_STATIC_FLIST_GET(entries), e) < 0)
        pa_xfree(e);

    pa_assert(s->n_entries >= 1);
    s->n_entries--;
}

void pa_idxset_free(pa_idxset *s, pa_free_cb_t free_cb) {
    pa_assert(s);

    pa_idxset_remove_all(s, free_cb);
    pa_xfree(s);
}

static struct idxset_entry* data_scan(pa_idxset *s, unsigned hash, const void *p) {
    struct idxset_entry *e;
    pa_assert(s);
    pa_assert(hash < NBUCKETS);
    pa_assert(p);

    for (e = BY_DATA(s)[hash]; e; e = e->data_next)
        if (s->compare_func(e->data, p) == 0)
            return e;

    return NULL;
}

static struct idxset_entry* index_scan(pa_idxset *s, unsigned hash, uint32_t idx) {
    struct idxset_entry *e;
    pa_assert(s);
    pa_assert(hash < NBUCKETS);

    for (e = BY_INDEX(s)[hash]; e; e = e->index_next)
        if (e->idx == idx)
            return e;

    return NULL;
}

int pa_idxset_put(pa_idxset*s, void *p, uint32_t *idx) {
    unsigned hash;
    struct idxset_entry *e;

    pa_assert(s);

    hash = s->hash_func(p) % NBUCKETS;

    if ((e = data_scan(s, hash, p))) {
        if (idx)
            *idx = e->idx;

        return -1;
    }

    if (!(e = pa_flist_pop(PA_STATIC_FLIST_GET(entries))))
        e = pa_xnew(struct idxset_entry, 1);

    e->data = p;
    e->idx = s->current_index++;

    /* Insert into data hash table */
    e->data_next = BY_DATA(s)[hash];
    e->data_previous = NULL;
    if (BY_DATA(s)[hash])
        BY_DATA(s)[hash]->data_previous = e;
    BY_DATA(s)[hash] = e;

    hash = e->idx % NBUCKETS;

    /* Insert into index hash table */
    e->index_next = BY_INDEX(s)[hash];
    e->index_previous = NULL;
    if (BY_INDEX(s)[hash])
        BY_INDEX(s)[hash]->index_previous = e;
    BY_INDEX(s)[hash] = e;

    /* Insert into iteration list */
    e->iterate_previous = s->iterate_list_tail;
    e->iterate_next = NULL;
    if (s->iterate_list_tail) {
        pa_assert(s->iterate_list_head);
        s->iterate_list_tail->iterate_next = e;
    } else {
        pa_assert(!s->iterate_list_head);
        s->iterate_list_head = e;
    }
    s->iterate_list_tail = e;

    s->n_entries++;
    pa_assert(s->n_entries >= 1);

    if (idx)
        *idx = e->idx;

    return 0;
}

void* pa_idxset_get_by_index(pa_idxset*s, uint32_t idx) {
    unsigned hash;
    struct idxset_entry *e;

    pa_assert(s);

    hash = idx % NBUCKETS;

    if (!(e = index_scan(s, hash, idx)))
        return NULL;

    return e->data;
}

void* pa_idxset_get_by_data(pa_idxset*s, const void *p, uint32_t *idx) {
    unsigned hash;
    struct idxset_entry *e;

    pa_assert(s);

    hash = s->hash_func(p) % NBUCKETS;

    if (!(e = data_scan(s, hash, p)))
        return NULL;

    if (idx)
        *idx = e->idx;

    return e->data;
}

void* pa_idxset_remove_by_index(pa_idxset*s, uint32_t idx) {
    struct idxset_entry *e;
    unsigned hash;
    void *data;

    pa_assert(s);

    hash = idx % NBUCKETS;

    if (!(e = index_scan(s, hash, idx)))
        return NULL;

    data = e->data;
    remove_entry(s, e);

    return data;
}

void* pa_idxset_remove_by_data(pa_idxset*s, const void *data, uint32_t *idx) {
    struct idxset_entry *e;
    unsigned hash;
    void *r;

    pa_assert(s);

    hash = s->hash_func(data) % NBUCKETS;

    if (!(e = data_scan(s, hash, data)))
        return NULL;

    r = e->data;

    if (idx)
        *idx = e->idx;

    remove_entry(s, e);

    return r;
}

void pa_idxset_remove_all(pa_idxset *s, pa_free_cb_t free_cb) {
    pa_assert(s);

    while (s->iterate_list_head) {
        void *data = s->iterate_list_head->data;

        remove_entry(s, s->iterate_list_head);

        if (free_cb)
            free_cb(data);
    }
}

void* pa_idxset_rrobin(pa_idxset *s, uint32_t *idx) {
    unsigned hash;
    struct idxset_entry *e;

    pa_assert(s);
    pa_assert(idx);

    hash = *idx % NBUCKETS;

    e = index_scan(s, hash, *idx);

    if (e && e->iterate_next)
        e = e->iterate_next;
    else
        e = s->iterate_list_head;

    if (!e)
        return NULL;

    *idx = e->idx;
    return e->data;
}

void *pa_idxset_iterate(pa_idxset *s, void **state, uint32_t *idx) {
    struct idxset_entry *e;

    pa_assert(s);
    pa_assert(state);

    if (*state == (void*) -1)
        goto at_end;

    if ((!*state && !s->iterate_list_head))
        goto at_end;

    e = *state ? *state : s->iterate_list_head;

    if (e->iterate_next)
        *state = e->iterate_next;
    else
        *state = (void*) -1;

    if (idx)
        *idx = e->idx;

    return e->data;

at_end:
    *state = (void *) -1;

    if (idx)
        *idx = PA_IDXSET_INVALID;

    return NULL;
}

void* pa_idxset_steal_first(pa_idxset *s, uint32_t *idx) {
    void *data;

    pa_assert(s);

    if (!s->iterate_list_head)
        return NULL;

    data = s->iterate_list_head->data;

    if (idx)
        *idx = s->iterate_list_head->idx;

    remove_entry(s, s->iterate_list_head);

    return data;
}

void* pa_idxset_first(pa_idxset *s, uint32_t *idx) {
    pa_assert(s);

    if (!s->iterate_list_head) {
        if (idx)
            *idx = PA_IDXSET_INVALID;
        return NULL;
    }

    if (idx)
        *idx = s->iterate_list_head->idx;

    return s->iterate_list_head->data;
}

void *pa_idxset_next(pa_idxset *s, uint32_t *idx) {
    struct idxset_entry *e;
    unsigned hash;

    pa_assert(s);
    pa_assert(idx);

    if (*idx == PA_IDXSET_INVALID)
        return NULL;

    hash = *idx % NBUCKETS;

    if ((e = index_scan(s, hash, *idx))) {

        e = e->iterate_next;

        if (e) {
            *idx = e->idx;
            return e->data;
        } else {
            *idx = PA_IDXSET_INVALID;
            return NULL;
        }

    } else {

        /* If the entry passed doesn't exist anymore we try to find
         * the next following */

        for ((*idx)++; *idx < s->current_index; (*idx)++) {

            hash = *idx % NBUCKETS;

            if ((e = index_scan(s, hash, *idx))) {
                *idx = e->idx;
                return e->data;
            }
        }

        *idx = PA_IDXSET_INVALID;
        return NULL;
    }
}

unsigned pa_idxset_size(pa_idxset*s) {
    pa_assert(s);

    return s->n_entries;
}

bool pa_idxset_isempty(pa_idxset *s) {
    pa_assert(s);

    return s->n_entries == 0;
}

pa_idxset *pa_idxset_copy(pa_idxset *s, pa_copy_func_t copy_func) {
    pa_idxset *copy;
    struct idxset_entry *i;

    pa_assert(s);

    copy = pa_idxset_new(s->hash_func, s->compare_func);

    for (i = s->iterate_list_head; i; i = i->iterate_next)
        pa_idxset_put(copy, copy_func ? copy_func(i->data) : i->data, NULL);

    return copy;
}
