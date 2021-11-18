#ifndef foopulseshmasyncqhfoo
#define foopulseshmasyncqhfoo

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

#include <pulsecore/macro.h>

/* Similar to pa_asyncq, but stores data in a shared memory segment */

struct pa_shmasyncq_data {
    unsigned n_elements;
    size_t element_size;
    unsigned read_idx;
    unsigned write_idx;
    pa_fdsem_data read_fdsem_data, write_fdsem_data;
};

#define PA_SHMASYNCQ_DEFAULT_N_ELEMENTS 128
#define PA_SHMASYNCQ_SIZE(n_elements, element_size) (PA_ALIGN(sizeof(pa_shmasyncq_data)) + (((n_elements) * (PA_ALIGN(sizeof(pa_atomic_t)) + PA_ALIGN(element_size)))))
#define PA_SHMASYNCQ_DEFAULT_SIZE(element_size) PA_SHMASYNCQ_SIZE(PA_SHMASYNCQ_DEFAULT_N_ELEMENTS, element_size)

typedef struct pa_shmasyncq pa_shmasyncq;

pa_shmasyncq *pa_shmasyncq_new(unsigned n_elements, size_t element_size, void *data, int fd[2]);
void pa_shmasyncq_free(pa_shmasyncq* q, pa_free_cb_t free_cb);

void* pa_shmasyncq_pop_begin(pa_shmasyncq *q, bool wait);
void pa_shmasyncq_pop_commit(pa_shmasyncq *q);

int* pa_shmasyncq_push_begin(pa_shmasyncq *q, bool wait);
void pa_shmasyncq_push_commit(pa_shmasyncq *q);

int pa_shmasyncq_get_fd(pa_shmasyncq *q);
int pa_shmasyncq_before_poll(pa_shmasyncq *a);
void pa_shmasyncq_after_poll(pa_shmasyncq *a);

#endif
