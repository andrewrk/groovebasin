#ifndef foopulsefdsemhfoo
#define foopulsefdsemhfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <sys/types.h>

/* A simple, asynchronous semaphore which uses fds for sleeping. In
 * the best case all functions are lock-free unless sleeping is
 * required.  */

#include <pulsecore/atomic.h>

typedef struct pa_fdsem pa_fdsem;

typedef struct pa_fdsem_data {
    pa_atomic_t waiting;
    pa_atomic_t signalled;
    pa_atomic_t in_pipe;
} pa_fdsem_data;

pa_fdsem *pa_fdsem_new(void);
pa_fdsem *pa_fdsem_open_shm(pa_fdsem_data *data, int event_fd);
pa_fdsem *pa_fdsem_new_shm(pa_fdsem_data *data);
void pa_fdsem_free(pa_fdsem *f);

void pa_fdsem_post(pa_fdsem *f);
void pa_fdsem_wait(pa_fdsem *f);
int pa_fdsem_try(pa_fdsem *f);

int pa_fdsem_get(pa_fdsem *f);

int pa_fdsem_before_poll(pa_fdsem *f);
int pa_fdsem_after_poll(pa_fdsem *f);

#endif
