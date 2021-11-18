#ifndef foopulsecoredynarrayhfoo
#define foopulsecoredynarrayhfoo

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

#include <pulse/def.h>

typedef struct pa_dynarray pa_dynarray;

/* Implementation of a simple dynamically sized array for storing pointers.
 *
 * When the array is created, a free callback can be provided, which will be
 * then used when removing items from the array and when freeing the array. If
 * the free callback is not provided, the memory management of the stored items
 * is the responsibility of the array user. If there is need to remove items
 * from the array without freeing them, while also having the free callback
 * set, the functions with "steal" in their name can be used.
 *
 * Removing items from the middle of the array causes the last item to be
 * moved to the place of the removed item. That is, array ordering is not
 * preserved.
 *
 * The array doesn't support storing NULL pointers. */

pa_dynarray* pa_dynarray_new(pa_free_cb_t free_cb);
void pa_dynarray_free(pa_dynarray *array);

void pa_dynarray_append(pa_dynarray *array, void *p);

/* Returns the element at index i, or NULL if i is out of bounds. */
void *pa_dynarray_get(pa_dynarray *array, unsigned i);

/* Returns the last element, or NULL if the array is empty. */
void *pa_dynarray_last(pa_dynarray *array);

/* Returns -PA_ERR_NOENTITY if i is out of bounds, and zero otherwise. */
int pa_dynarray_remove_by_index(pa_dynarray *array, unsigned i);

/* Returns -PA_ERR_NOENTITY if p is not found in the array, and zero
 * otherwise. If the array contains multiple occurrences of p, only one of
 * them is removed (and it's unspecified which one). */
int pa_dynarray_remove_by_data(pa_dynarray *array, void *p);

/* Returns the removed item, or NULL if the array is empty. */
void *pa_dynarray_steal_last(pa_dynarray *array);

unsigned pa_dynarray_size(pa_dynarray *array);

/* Returns -PA_ERR_NOENTITY if i is out of bounds, and zero otherwise.
 * Here i is the location index in the array like 0, ..., array->entries */
int pa_dynarray_insert_by_index(pa_dynarray *array, void *p, unsigned i);

#define PA_DYNARRAY_FOREACH(elem, array, idx) \
    for ((idx) = 0; ((elem) = pa_dynarray_get(array, idx)); (idx)++)

#endif
