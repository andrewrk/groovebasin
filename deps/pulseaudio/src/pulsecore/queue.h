#ifndef foopulsecorequeuehfoo
#define foopulsecorequeuehfoo

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

typedef struct pa_queue pa_queue;

/* A simple implementation of the abstract data type queue. Stores
 * pointers as members. The memory has to be managed by the caller. */

pa_queue* pa_queue_new(void);

/* Free the queue and run the specified callback function for every
 * remaining entry. The callback function may be NULL. */
void pa_queue_free(pa_queue *q, pa_free_cb_t free_func);

void pa_queue_push(pa_queue *q, void *p);
void* pa_queue_pop(pa_queue *q);

int pa_queue_isempty(pa_queue *q);

#endif
