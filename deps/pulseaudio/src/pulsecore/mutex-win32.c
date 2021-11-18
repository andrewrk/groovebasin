/***
  This file is part of PulseAudio.

  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <windows.h>

#include <pulse/xmalloc.h>
#include <pulsecore/hashmap.h>

#include "mutex.h"

struct pa_mutex {
    CRITICAL_SECTION mutex;
};

struct pa_cond {
    pa_hashmap *wait_events;
};

pa_mutex* pa_mutex_new(bool recursive, bool inherit_priority) {
    pa_mutex *m;

    m = pa_xnew(pa_mutex, 1);

    InitializeCriticalSection(&m->mutex);

    return m;
}

void pa_mutex_free(pa_mutex *m) {
    assert(m);

    DeleteCriticalSection(&m->mutex);
    pa_xfree(m);
}

void pa_mutex_lock(pa_mutex *m) {
    assert(m);

    EnterCriticalSection(&m->mutex);
}

void pa_mutex_unlock(pa_mutex *m) {
    assert(m);

    LeaveCriticalSection(&m->mutex);
}

pa_cond *pa_cond_new(void) {
    pa_cond *c;

    c = pa_xnew(pa_cond, 1);
    c->wait_events = pa_hashmap_new(NULL, NULL);
    assert(c->wait_events);

    return c;
}

void pa_cond_free(pa_cond *c) {
    assert(c);

    pa_hashmap_free(c->wait_events);
    pa_xfree(c);
}

void pa_cond_signal(pa_cond *c, int broadcast) {
    assert(c);

    if (pa_hashmap_size(c->wait_events) == 0)
        return;

    if (broadcast)
        SetEvent(pa_hashmap_first(c->wait_events));
    else {
        void *iter;
        const void *key;
        HANDLE event;

        iter = NULL;
        while (1) {
            pa_hashmap_iterate(c->wait_events, &iter, &key);
            if (key == NULL)
                break;
            event = (HANDLE)pa_hashmap_get(c->wait_events, key);
            SetEvent(event);
        }
    }
}

int pa_cond_wait(pa_cond *c, pa_mutex *m) {
    HANDLE event;

    assert(c);
    assert(m);

    event = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(event);

    pa_hashmap_put(c->wait_events, event, event);

    pa_mutex_unlock(m);

    WaitForSingleObject(event, INFINITE);

    pa_mutex_lock(m);

    pa_hashmap_remove(c->wait_events, event);

    CloseHandle(event);

    return 0;
}

/* This is a copy of the function in mutex-posix.c */
pa_mutex* pa_static_mutex_get(pa_static_mutex *s, bool recursive, bool inherit_priority) {
    pa_mutex *m;

    pa_assert(s);

    /* First, check if already initialized and short cut */
    if ((m = pa_atomic_ptr_load(&s->ptr)))
        return m;

    /* OK, not initialized, so let's allocate, and fill in */
    m = pa_mutex_new(recursive, inherit_priority);
    if ((pa_atomic_ptr_cmpxchg(&s->ptr, NULL, m)))
        return m;

    pa_mutex_free(m);

    /* Him, filling in failed, so someone else must have filled in
     * already */
    pa_assert_se(m = pa_atomic_ptr_load(&s->ptr));
    return m;
}
