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

#include <string.h>
#include <stdlib.h>

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>

#include "dynarray.h"

struct pa_dynarray {
    void **data;
    unsigned n_allocated, n_entries;
    pa_free_cb_t free_cb;
};

pa_dynarray* pa_dynarray_new(pa_free_cb_t free_cb) {
    pa_dynarray *array;

    array = pa_xnew0(pa_dynarray, 1);
    array->free_cb = free_cb;

    return array;
}

void pa_dynarray_free(pa_dynarray *array) {
    unsigned i;
    pa_assert(array);

    if (array->free_cb)
        for (i = 0; i < array->n_entries; i++)
            array->free_cb(array->data[i]);

    pa_xfree(array->data);
    pa_xfree(array);
}

void pa_dynarray_append(pa_dynarray *array, void *p) {
    pa_assert(array);
    pa_assert(p);

    if (array->n_entries == array->n_allocated) {
        unsigned n = PA_MAX(array->n_allocated * 2, 25U);

        array->data = pa_xrealloc(array->data, sizeof(void *) * n);
        array->n_allocated = n;
    }

    array->data[array->n_entries++] = p;
}

void *pa_dynarray_get(pa_dynarray *array, unsigned i) {
    pa_assert(array);

    if (i >= array->n_entries)
        return NULL;

    return array->data[i];
}

void *pa_dynarray_last(pa_dynarray *array) {
    pa_assert(array);

    if (array->n_entries == 0)
        return NULL;

    return array->data[array->n_entries - 1];
}

int pa_dynarray_remove_by_index(pa_dynarray *array, unsigned i) {
    void *entry;

    pa_assert(array);

    if (i >= array->n_entries)
        return -PA_ERR_NOENTITY;

    entry = array->data[i];
    array->data[i] = array->data[array->n_entries - 1];
    array->n_entries--;

    if (array->free_cb)
        array->free_cb(entry);

    return 0;
}

int pa_dynarray_remove_by_data(pa_dynarray *array, void *p) {
    unsigned i;

    pa_assert(array);
    pa_assert(p);

    /* Iterate backwards, with the assumption that recently appended entries
     * are likely to be removed first. */
    i = array->n_entries;
    while (i > 0) {
        i--;
        if (array->data[i] == p) {
            pa_dynarray_remove_by_index(array, i);
            return 0;
        }
    }

    return -PA_ERR_NOENTITY;
}

void *pa_dynarray_steal_last(pa_dynarray *array) {
    pa_assert(array);

    if (array->n_entries > 0)
        return array->data[--array->n_entries];
    else
        return NULL;
}

unsigned pa_dynarray_size(pa_dynarray *array) {
    pa_assert(array);

    return array->n_entries;
}

int pa_dynarray_insert_by_index(pa_dynarray *array, void *p, unsigned i) {
    void *entry;
    unsigned j;

    pa_assert(array);

    if (i > array->n_entries)
        return -PA_ERR_NOENTITY;

    if (i == array->n_entries)
        pa_dynarray_append(array, p);
    else {
        entry = pa_dynarray_last(array);
        pa_dynarray_append(array, entry);
        j = array->n_entries - 2;
        for (;j > i; j--)
	    array->data[j] = array->data[j-1];
        array->data[i] = p;
    }

    return 0;
}
