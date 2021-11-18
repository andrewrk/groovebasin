/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering
  Copyright 2013 Albert Zeyer

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

#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <unistd.h>

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>
#include <pulsecore/atomic.h>
#include <pulsecore/core-util.h>

#include "semaphore.h"

/* OSX doesn't support unnamed semaphores (via sem_init).
 * Thus, we use a counter to give them enumerated names. */
static pa_atomic_t id_counter = PA_ATOMIC_INIT(0);

struct pa_semaphore {
    sem_t *sem;
    int id;
};

static char *sem_name(char *fn, size_t l, int id) {
    pa_snprintf(fn, l, "/pulse-sem-%u-%u", getpid(), id);
    return fn;
}

pa_semaphore *pa_semaphore_new(unsigned value) {
    pa_semaphore *s;
    char fn[32];

    s = pa_xnew(pa_semaphore, 1);
    s->id = pa_atomic_inc(&id_counter);
    sem_name(fn, sizeof(fn), s->id);
    sem_unlink(fn); /* in case an old stale semaphore is left around */
    pa_assert_se(s->sem = sem_open(fn, O_CREAT|O_EXCL, 0700, value));
    pa_assert(s->sem != SEM_FAILED);
    return s;
}

void pa_semaphore_free(pa_semaphore *s) {
    char fn[32];

    pa_assert(s);

    pa_assert_se(sem_close(s->sem) == 0);
    sem_name(fn, sizeof(fn), s->id);
    pa_assert_se(sem_unlink(fn) == 0);
    pa_xfree(s);
}

void pa_semaphore_post(pa_semaphore *s) {
    pa_assert(s);
    pa_assert_se(sem_post(s->sem) == 0);
}

void pa_semaphore_wait(pa_semaphore *s) {
    int ret;

    pa_assert(s);

    do {
        ret = sem_wait(s->sem);
    } while (ret < 0 && errno == EINTR);

    pa_assert(ret == 0);
}
