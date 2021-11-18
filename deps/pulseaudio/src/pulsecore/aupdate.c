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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/semaphore.h>
#include <pulsecore/macro.h>
#include <pulsecore/mutex.h>

#include "aupdate.h"

#define MSB (1U << (sizeof(unsigned)*8U-1))
#define WHICH(n) (!!((n) & MSB))
#define COUNTER(n) ((n) & ~MSB)

struct pa_aupdate {
    pa_atomic_t read_lock;
    pa_mutex *write_lock;
    pa_semaphore *semaphore;
    bool swapped;
};

pa_aupdate *pa_aupdate_new(void) {
    pa_aupdate *a;

    a = pa_xnew(pa_aupdate, 1);
    pa_atomic_store(&a->read_lock, 0);
    a->write_lock = pa_mutex_new(false, false);
    a->semaphore = pa_semaphore_new(0);

    return a;
}

void pa_aupdate_free(pa_aupdate *a) {
    pa_assert(a);

    pa_mutex_free(a->write_lock);
    pa_semaphore_free(a->semaphore);

    pa_xfree(a);
}

unsigned pa_aupdate_read_begin(pa_aupdate *a) {
    unsigned n;

    pa_assert(a);

    /* Increase the lock counter */
    n = (unsigned) pa_atomic_inc(&a->read_lock);

    /* When n is 0 we have about 2^31 threads running that all try to
     * access the data at the same time, oh my! */
    pa_assert(COUNTER(n)+1 > 0);

    /* The uppermost bit tells us which data to look at */
    return WHICH(n);
}

void pa_aupdate_read_end(pa_aupdate *a) {
    unsigned PA_UNUSED n;

    pa_assert(a);

    /* Decrease the lock counter */
    n = (unsigned) pa_atomic_dec(&a->read_lock);

    /* Make sure the counter was valid */
    pa_assert(COUNTER(n) > 0);

    /* Post the semaphore */
    pa_semaphore_post(a->semaphore);
}

unsigned pa_aupdate_write_begin(pa_aupdate *a) {
    unsigned n;

    pa_assert(a);

    pa_mutex_lock(a->write_lock);

    n = (unsigned) pa_atomic_load(&a->read_lock);

    a->swapped = false;

    return !WHICH(n);
}

unsigned pa_aupdate_write_swap(pa_aupdate *a) {
    unsigned n;

    pa_assert(a);

    for (;;) {
        n = (unsigned) pa_atomic_load(&a->read_lock);

        /* If the read counter is > 0 wait; if it is 0 try to swap the lists */
        if (COUNTER(n) > 0)
            pa_semaphore_wait(a->semaphore);
        else if (pa_atomic_cmpxchg(&a->read_lock, (int) n, (int) (n ^ MSB)))
            break;
    }

    a->swapped = true;

    return WHICH(n);
}

void pa_aupdate_write_end(pa_aupdate *a) {
    pa_assert(a);

    if (!a->swapped)
        pa_aupdate_write_swap(a);

    pa_mutex_unlock(a->write_lock);
}
