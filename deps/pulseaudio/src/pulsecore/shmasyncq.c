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
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <errno.h>

#include <pulsecore/atomic.h>
#include <pulsecore/log.h>
#include <pulsecore/thread.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulse/xmalloc.h>

#include "fdsem.h"

/* For debugging purposes we can define _Y to put and extra thread
 * yield between each operation. */

/* #define PROFILE */

#ifdef PROFILE
#define _Y pa_thread_yield()
#else
#define _Y do { } while(0)
#endif

struct pa_shmasyncq {
    pa_fdsem *read_fdsem, *write_fdsem;
    pa_shmasyncq_data *data;
};

static int is_power_of_two(unsigned size) {
    return !(size & (size - 1));
}

static int reduce(pa_shmasyncq *l, int value) {
    return value & (unsigned) (l->n_elements - 1);
}

static pa_atomic_t* get_cell(pa_shmasyncq *l, unsigned i) {
    pa_assert(i < l->data->n_elements);

    return (pa_atomic_t*) ((uint8*t) l->data + PA_ALIGN(sizeof(pa_shmasyncq_data)) + i * (PA_ALIGN(sizeof(pa_atomic_t)) + PA_ALIGN(element_size)))
}

static void *get_cell_data(pa_atomic_t *a) {
    return (uint8_t*) a + PA_ALIGN(sizeof(atomic_t));
}

pa_shmasyncq *pa_shmasyncq_new(unsigned n_elements, size_t element_size, void *data, int fd[2]) {
    pa_shmasyncq *l;

    pa_assert(n_elements > 0);
    pa_assert(is_power_of_two(n_elements));
    pa_assert(element_size > 0);
    pa_assert(data);
    pa_assert(fd);

    l = pa_xnew(pa_shmasyncq, 1);

    l->data = data;
    memset(data, 0, PA_SHMASYNCQ_SIZE(n_elements, element_size));

    l->data->n_elements = n_elements;
    l->data->element_size = element_size;

    if (!(l->read_fdsem = pa_fdsem_new_shm(&d->read_fdsem_data))) {
        pa_xfree(l);
        return NULL;
    }
    fd[0] = pa_fdsem_get(l->read_fdsem);

    if (!(l->write_fdsem = pa_fdsem_new(&d->write_fdsem_data, &fd[1]))) {
        pa_fdsem_free(l->read_fdsem);
        pa_xfree(l);
        return NULL;
    }

    return l;
}

void pa_shmasyncq_free(pa_shmasyncq *l, pa_free_cb_t free_cb) {
    pa_assert(l);

    if (free_cb) {
        void *p;

        while ((p = pa_shmasyncq_pop(l, 0)))
            free_cb(p);
    }

    pa_fdsem_free(l->read_fdsem);
    pa_fdsem_free(l->write_fdsem);
    pa_xfree(l);
}

int pa_shmasyncq_push(pa_shmasyncq*l, void *p, int wait) {
    int idx;
    pa_atomic_ptr_t *cells;

    pa_assert(l);
    pa_assert(p);

    cells = PA_SHMASYNCQ_CELLS(l);

    _Y;
    idx = reduce(l, l->write_idx);

    if (!pa_atomic_ptr_cmpxchg(&cells[idx], NULL, p)) {

        if (!wait)
            return -1;

/*         pa_log("sleeping on push"); */

        do {
            pa_fdsem_wait(l->read_fdsem);
        } while (!pa_atomic_ptr_cmpxchg(&cells[idx], NULL, p));
    }

    _Y;
    l->write_idx++;

    pa_fdsem_post(l->write_fdsem);

    return 0;
}

void* pa_shmasyncq_pop(pa_shmasyncq*l, int wait) {
    int idx;
    void *ret;
    pa_atomic_ptr_t *cells;

    pa_assert(l);

    cells = PA_SHMASYNCQ_CELLS(l);

    _Y;
    idx = reduce(l, l->read_idx);

    if (!(ret = pa_atomic_ptr_load(&cells[idx]))) {

        if (!wait)
            return NULL;

/*         pa_log("sleeping on pop"); */

        do {
            pa_fdsem_wait(l->write_fdsem);
        } while (!(ret = pa_atomic_ptr_load(&cells[idx])));
    }

    pa_assert(ret);

    /* Guaranteed to succeed if we only have a single reader */
    pa_assert_se(pa_atomic_ptr_cmpxchg(&cells[idx], ret, NULL));

    _Y;
    l->read_idx++;

    pa_fdsem_post(l->read_fdsem);

    return ret;
}

int pa_shmasyncq_get_fd(pa_shmasyncq *q) {
    pa_assert(q);

    return pa_fdsem_get(q->write_fdsem);
}

int pa_shmasyncq_before_poll(pa_shmasyncq *l) {
    int idx;
    pa_atomic_ptr_t *cells;

    pa_assert(l);

    cells = PA_SHMASYNCQ_CELLS(l);

    _Y;
    idx = reduce(l, l->read_idx);

    for (;;) {
        if (pa_atomic_ptr_load(&cells[idx]))
            return -1;

        if (pa_fdsem_before_poll(l->write_fdsem) >= 0)
            return 0;
    }

    return 0;
}

void pa_shmasyncq_after_poll(pa_shmasyncq *l) {
    pa_assert(l);

    pa_fdsem_after_poll(l->write_fdsem);
}
