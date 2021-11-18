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

#include <signal.h>

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

/* This is deprecated on glibc but is still used by FreeBSD */
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>
#include <pulsecore/aupdate.h>
#include <pulsecore/atomic.h>
#include <pulsecore/once.h>
#include <pulsecore/mutex.h>

#include "memtrap.h"

struct pa_memtrap {
    void *start;
    size_t size;
    pa_atomic_t bad;
    pa_memtrap *next[2], *prev[2];
};

static pa_memtrap *memtraps[2] = { NULL, NULL };
static pa_aupdate *aupdate;
static pa_static_mutex mutex = PA_STATIC_MUTEX_INIT; /* only required to serialize access to the write side */

static void allocate_aupdate(void) {
    PA_ONCE_BEGIN {
        aupdate = pa_aupdate_new();
    } PA_ONCE_END;
}

bool pa_memtrap_is_good(pa_memtrap *m) {
    pa_assert(m);

    return !pa_atomic_load(&m->bad);
}

#ifdef HAVE_SIGACTION
static void sigsafe_error(const char *s) {
    size_t ret PA_GCC_UNUSED;
    ret = write(STDERR_FILENO, s, strlen(s));
}

static void signal_handler(int sig, siginfo_t* si, void *data) {
    unsigned j;
    pa_memtrap *m;
    void *r;

    j = pa_aupdate_read_begin(aupdate);

    for (m = memtraps[j]; m; m = m->next[j])
        if (si->si_addr >= m->start &&
            (uint8_t*) si->si_addr < (uint8_t*) m->start + m->size)
            break;

    if (!m)
        goto fail;

    pa_atomic_store(&m->bad, 1);

    /* Remap anonymous memory into the bad segment */
    if ((r = mmap(m->start, m->size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_FIXED|MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
        sigsafe_error("mmap() failed.\n");
        goto fail;
    }

    pa_assert(r == m->start);

    pa_aupdate_read_end(aupdate);
    return;

fail:
    pa_aupdate_read_end(aupdate);

    sigsafe_error("Failed to handle SIGBUS.\n");
    abort();
}
#endif

static void memtrap_link(pa_memtrap *m, unsigned j) {
    pa_assert(m);

    m->prev[j] = NULL;

    if ((m->next[j] = memtraps[j]))
        m->next[j]->prev[j] = m;

    memtraps[j] = m;
}

static void memtrap_unlink(pa_memtrap *m, unsigned j) {
    pa_assert(m);

    if (m->next[j])
        m->next[j]->prev[j] = m->prev[j];

    if (m->prev[j])
        m->prev[j]->next[j] = m->next[j];
    else
        memtraps[j] = m->next[j];
}

pa_memtrap* pa_memtrap_add(const void *start, size_t size) {
    pa_memtrap *m = NULL;
    unsigned j;
    pa_mutex *mx;

    pa_assert(start);
    pa_assert(size > 0);

    start = PA_PAGE_ALIGN_PTR(start);
    size = PA_PAGE_ALIGN(size);

    m = pa_xnew(pa_memtrap, 1);
    m->start = (void*) start;
    m->size = size;
    pa_atomic_store(&m->bad, 0);

    allocate_aupdate();

    mx = pa_static_mutex_get(&mutex, false, true);
    pa_mutex_lock(mx);

    j = pa_aupdate_write_begin(aupdate);
    memtrap_link(m, j);
    j = pa_aupdate_write_swap(aupdate);
    memtrap_link(m, j);
    pa_aupdate_write_end(aupdate);

    pa_mutex_unlock(mx);

    return m;
}

void pa_memtrap_remove(pa_memtrap *m) {
    unsigned j;
    pa_mutex *mx;

    pa_assert(m);

    allocate_aupdate();

    mx = pa_static_mutex_get(&mutex, false, true);
    pa_mutex_lock(mx);

    j = pa_aupdate_write_begin(aupdate);
    memtrap_unlink(m, j);
    j = pa_aupdate_write_swap(aupdate);
    memtrap_unlink(m, j);
    pa_aupdate_write_end(aupdate);

    pa_mutex_unlock(mx);

    pa_xfree(m);
}

pa_memtrap *pa_memtrap_update(pa_memtrap *m, const void *start, size_t size) {
    unsigned j;
    pa_mutex *mx;

    pa_assert(m);

    pa_assert(start);
    pa_assert(size > 0);

    start = PA_PAGE_ALIGN_PTR(start);
    size = PA_PAGE_ALIGN(size);

    allocate_aupdate();

    mx = pa_static_mutex_get(&mutex, false, true);
    pa_mutex_lock(mx);

    j = pa_aupdate_write_begin(aupdate);

    if (m->start == start && m->size == size)
        goto unlock;

    memtrap_unlink(m, j);
    pa_aupdate_write_swap(aupdate);

    m->start = (void*) start;
    m->size = size;
    pa_atomic_store(&m->bad, 0);

    pa_assert_se(pa_aupdate_write_swap(aupdate) == j);
    memtrap_link(m, j);

unlock:
    pa_aupdate_write_end(aupdate);

    pa_mutex_unlock(mx);

    return m;
}

void pa_memtrap_install(void) {
#ifdef HAVE_SIGACTION
    struct sigaction sa;

    allocate_aupdate();

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_RESTART|SA_SIGINFO;

    pa_assert_se(sigaction(SIGBUS, &sa, NULL) == 0);
#ifdef __FreeBSD_kernel__
    pa_assert_se(sigaction(SIGSEGV, &sa, NULL) == 0);
#endif
#endif
}
