#ifndef foopulsemutexhfoo
#define foopulsemutexhfoo

/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering

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

#include <pulsecore/macro.h>
#include <pulsecore/atomic.h>

typedef struct pa_mutex pa_mutex;

/* Please think twice before enabling priority inheritance. This is no
 * magic wand! Use it only when the potentially prioritized threads are
 * good candidates for it. Don't use this blindly! Also, note that
 * only very few operating systems actually implement this, hence this
 * is merely a hint. */
pa_mutex* pa_mutex_new(bool recursive, bool inherit_priority);

void pa_mutex_free(pa_mutex *m);
void pa_mutex_lock(pa_mutex *m);
bool pa_mutex_try_lock(pa_mutex *m);
void pa_mutex_unlock(pa_mutex *m);

typedef struct pa_cond pa_cond;

pa_cond *pa_cond_new(void);
void pa_cond_free(pa_cond *c);
void pa_cond_signal(pa_cond *c, int broadcast);
int pa_cond_wait(pa_cond *c, pa_mutex *m);

/* Static mutexes are basically just atomically updated pointers to pa_mutex objects */

typedef struct pa_static_mutex {
    pa_atomic_ptr_t ptr;
} pa_static_mutex;

#define PA_STATIC_MUTEX_INIT { PA_ATOMIC_PTR_INIT(NULL) }

pa_mutex* pa_static_mutex_get(pa_static_mutex *m, bool recursive, bool inherit_priority);

#endif
