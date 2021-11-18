#ifndef foopulsecoreidxsethfoo
#define foopulsecoreidxsethfoo

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

#include <inttypes.h>

#include <pulse/def.h>

#include <pulsecore/macro.h>

/* A combination of a set and a dynamic array. Entries are indexable
 * both through an automatically generated numeric index and the
 * entry's data pointer. As usual, memory management is the user's
 * job. */

/* A special index value denoting the invalid index. */
#define PA_IDXSET_INVALID ((uint32_t) -1)

/* Generic implementations for hash and comparison functions. Just
 * compares the pointer or calculates the hash value directly from the
 * pointer value. */
unsigned pa_idxset_trivial_hash_func(const void *p);
int pa_idxset_trivial_compare_func(const void *a, const void *b);

/* Generic implementations for hash and comparison functions for strings. */
unsigned pa_idxset_string_hash_func(const void *p);
int pa_idxset_string_compare_func(const void *a, const void *b);

typedef unsigned (*pa_hash_func_t)(const void *p);
typedef int (*pa_compare_func_t)(const void *a, const void *b);
typedef void *(*pa_copy_func_t)(const void *p);

typedef struct pa_idxset pa_idxset;

/* Instantiate a new idxset with the specified hash and comparison functions */
pa_idxset* pa_idxset_new(pa_hash_func_t hash_func, pa_compare_func_t compare_func);

/* Free the idxset. When the idxset is not empty the specified function is called for every entry contained */
void pa_idxset_free(pa_idxset *s, pa_free_cb_t free_cb);

/* Store a new item in the idxset. The index of the item is returned in *idx */
int pa_idxset_put(pa_idxset*s, void *p, uint32_t *idx);

/* Get the entry by its idx */
void* pa_idxset_get_by_index(pa_idxset*s, uint32_t idx);

/* Get the entry by its data. The index is returned in *idx */
void* pa_idxset_get_by_data(pa_idxset*s, const void *p, uint32_t *idx);

/* Similar to pa_idxset_get_by_index(), but removes the entry from the idxset. */
void* pa_idxset_remove_by_index(pa_idxset*s, uint32_t idx);

/* Similar to pa_idxset_get_by_data(), but removes the entry from the idxset */
void* pa_idxset_remove_by_data(pa_idxset*s, const void *p, uint32_t *idx);

/* If free_cb is not NULL, it's called for each entry. */
void pa_idxset_remove_all(pa_idxset *s, pa_free_cb_t free_cb);

/* This may be used to iterate through all entries. When called with
   an invalid index value it returns the first entry, otherwise the
   next following. The function is best called with *idx =
   PA_IDXSET_VALID first. It is safe to manipulate the idxset between
   the calls. It is not guaranteed that all entries have already been
   returned before the an entry is returned the second time.*/
void* pa_idxset_rrobin(pa_idxset *s, uint32_t *idx);

/* Iterate through the idxset. At first iteration state should be NULL */
void *pa_idxset_iterate(pa_idxset *s, void **state, uint32_t *idx);

/* Return the oldest entry in the idxset and remove it. If idx is not NULL fill in its index in *idx */
void* pa_idxset_steal_first(pa_idxset *s, uint32_t *idx);

/* Return the oldest entry in the idxset. Fill in its index in *idx. */
void* pa_idxset_first(pa_idxset *s, uint32_t *idx);

/* Return the entry following the entry indexed by *idx.  After the
 * call *index contains the index of the returned
 * object. pa_idxset_first() and pa_idxset_next() may be used to
 * iterate through the set.*/
void *pa_idxset_next(pa_idxset *s, uint32_t *idx);

/* Return the current number of entries in the idxset */
unsigned pa_idxset_size(pa_idxset*s);

/* Return true of the idxset is empty */
bool pa_idxset_isempty(pa_idxset *s);

/* Duplicate the idxset. This will not copy the actual indexes. If copy_func is
 * set, each entry is copied using the provided function, otherwise a shallow
 * copy will be made. */
pa_idxset *pa_idxset_copy(pa_idxset *s, pa_copy_func_t copy_func);

/* A macro to ease iteration through all entries */
#define PA_IDXSET_FOREACH(e, s, idx) \
    for ((e) = pa_idxset_first((s), &(idx)); (e); (e) = pa_idxset_next((s), &(idx)))

#endif
