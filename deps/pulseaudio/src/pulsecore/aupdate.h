#ifndef foopulsecoreaupdatehfoo
#define foopulsecoreaupdatehfoo

/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

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

typedef struct pa_aupdate pa_aupdate;

pa_aupdate *pa_aupdate_new(void);
void pa_aupdate_free(pa_aupdate *a);

/* Will return 0, or 1, depending on which copy of the data the caller
 * should look at */
unsigned pa_aupdate_read_begin(pa_aupdate *a);
void pa_aupdate_read_end(pa_aupdate *a);

/* Will return 0, or 1, depending which copy of the data the caller
 * should modify */
unsigned pa_aupdate_write_begin(pa_aupdate *a);
void pa_aupdate_write_end(pa_aupdate *a);

/* Will return 0, or 1, depending which copy of the data the caller
 * should modify. Each time called this will return the opposite of
 * the previous pa_aupdate_write_begin() / pa_aupdate_write_swap()
 * call. Should only be called between pa_aupdate_write_begin() and
 * pa_aupdate_write_end() */
unsigned pa_aupdate_write_swap(pa_aupdate *a);

/*
 * This infrastructure allows lock-free updates of arbitrary data
 * structures in an rcu'ish way: two copies of the data structure
 * should be existing. One side ('the reader') has read access to one
 * of the two data structure at a time. It does not have to lock it,
 * however it needs to signal that it is using it/stopped using
 * it. The other side ('the writer') modifies the second data structure,
 * and then atomically swaps the two data structures, followed by a
 * modification of the other one.
 *
 * This is intended to be used for cases where the reader side needs
 * to be fast while the writer side can be slow.
 *
 * The reader side is signal handler safe.
 *
 * The writer side lock is not recursive. The reader side is.
 *
 * There may be multiple readers and multiple writers at the same
 * time.
 *
 * Usage is like this:
 *
 * static struct foo bar[2];
 * static pa_aupdate *a;
 *
 * reader() {
 *     unsigned j;
 *
 *     j = pa_update_read_begin(a);
 *
 *     ... read the data structure bar[j] ...
 *
 *     pa_update_read_end(a);
 * }
 *
 * writer() {
 *    unsigned j;
 *
 *    j = pa_update_write_begin(a);
 *
 *    ... update the data structure bar[j] ...
 *
 *    j = pa_update_write_swap(a);
 *
 *    ... update the data structure bar[j], the same way as above ...
 *
 *    pa_update_write_end(a)
 * }
 *
 * In some cases keeping both structures up-to-date might not be
 * necessary, since they are fully rebuilt on each iteration
 * anyway. In that case you may leave the _write_swap() call out, it
 * will then be done implicitly in the _write_end() invocation.
 */

#endif
