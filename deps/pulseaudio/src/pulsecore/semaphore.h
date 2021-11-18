#ifndef foopulsesemaphorehfoo
#define foopulsesemaphorehfoo

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

typedef struct pa_semaphore pa_semaphore;

pa_semaphore* pa_semaphore_new(unsigned value);
void pa_semaphore_free(pa_semaphore *m);

void pa_semaphore_post(pa_semaphore *m);
void pa_semaphore_wait(pa_semaphore *m);

/* Static semaphores are basically just atomically updated pointers to
 * pa_semaphore objects */

typedef struct pa_static_semaphore {
    pa_atomic_ptr_t ptr;
} pa_static_semaphore;

#define PA_STATIC_SEMAPHORE_INIT { PA_ATOMIC_PTR_INIT(NULL) }

/* When you call this make sure to pass always the same value parameter! */
pa_semaphore* pa_static_semaphore_get(pa_static_semaphore *m, unsigned value);

#endif
