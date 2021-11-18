/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering
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

#ifndef OS_IS_WIN32
#include <pthread.h>
#endif

#include <signal.h>
#include <stdio.h>

#include <pulse/xmalloc.h>
#include <pulse/mainloop.h>

#include <pulsecore/i18n.h>
#include <pulsecore/log.h>
#include <pulsecore/thread.h>
#include <pulsecore/mutex.h>
#include <pulsecore/macro.h>
#include <pulsecore/poll.h>

#include "thread-mainloop.h"

struct pa_threaded_mainloop {
    pa_mainloop *real_mainloop;
    volatile int n_waiting, n_waiting_for_accept;
    pa_atomic_t in_once_unlocked;

    pa_thread* thread;
    pa_mutex* mutex;
    pa_cond* cond, *accept_cond;

    char *name;
};

static inline int in_worker(pa_threaded_mainloop *m) {
    return pa_thread_self() == m->thread;
}

static int poll_func(struct pollfd *ufds, unsigned long nfds, int timeout, void *userdata) {
    pa_mutex *mutex = userdata;
    int r;

    pa_assert(mutex);

    /* Before entering poll() we unlock the mutex, so that
     * avahi_simple_poll_quit() can succeed from another thread. */

    pa_mutex_unlock(mutex);
    r = pa_poll(ufds, nfds, timeout);
    pa_mutex_lock(mutex);

    return r;
}

static void thread(void *userdata) {
    pa_threaded_mainloop *m = userdata;

#ifndef OS_IS_WIN32
    sigset_t mask;
    sigset_t prev_mask;
    struct sigaction sa;

    sigfillset(&mask);

    /* If SIGSYS is currently unblocked and trapped then keep it unblocked. */
    if (!pthread_sigmask(SIG_SETMASK, NULL, &prev_mask) &&
        !sigismember(&prev_mask, SIGSYS) &&
        !sigaction(SIGSYS, NULL, &sa)
        && sa.sa_handler != SIG_DFL) {
        sigdelset(&mask, SIGSYS);
    }

    /* Make sure that signals are delivered to the main thread.
     * Use SIG_SETMASK because SIG_BLOCK does an union with current set.*/
    pthread_sigmask(SIG_SETMASK, &mask, NULL);
#endif

    pa_mutex_lock(m->mutex);

    (void) pa_mainloop_run(m->real_mainloop, NULL);

    pa_mutex_unlock(m->mutex);
}

pa_threaded_mainloop *pa_threaded_mainloop_new(void) {
    pa_threaded_mainloop *m;

    pa_init_i18n();

    m = pa_xnew0(pa_threaded_mainloop, 1);

    if (!(m->real_mainloop = pa_mainloop_new())) {
        pa_xfree(m);
        return NULL;
    }

    m->mutex = pa_mutex_new(true, true);
    m->cond = pa_cond_new();
    m->accept_cond = pa_cond_new();

    pa_mainloop_set_poll_func(m->real_mainloop, poll_func, m->mutex);

    return m;
}

void pa_threaded_mainloop_free(pa_threaded_mainloop* m) {
    pa_assert(m);

    /* Make sure that this function is not called from the helper thread */
    pa_assert((m->thread && !pa_thread_is_running(m->thread)) || !in_worker(m));

    pa_threaded_mainloop_stop(m);

    if (m->thread)
        pa_thread_free(m->thread);

    pa_mainloop_free(m->real_mainloop);

    pa_mutex_free(m->mutex);
    pa_cond_free(m->cond);
    pa_cond_free(m->accept_cond);

    pa_xfree(m->name);
    pa_xfree(m);
}

int pa_threaded_mainloop_start(pa_threaded_mainloop *m) {
    pa_assert(m);

    pa_assert(!m->thread || !pa_thread_is_running(m->thread));

    if (!(m->thread = pa_thread_new(m->name ? m->name : "threaded-ml", thread, m)))
        return -1;

    return 0;
}

void pa_threaded_mainloop_stop(pa_threaded_mainloop *m) {
    pa_assert(m);

    if (!m->thread || !pa_thread_is_running(m->thread))
        return;

    /* Make sure that this function is not called from the helper thread */
    pa_assert(!in_worker(m));

    pa_mutex_lock(m->mutex);
    pa_mainloop_quit(m->real_mainloop, 0);
    pa_mutex_unlock(m->mutex);

    pa_thread_join(m->thread);
}

void pa_threaded_mainloop_lock(pa_threaded_mainloop *m) {
    pa_assert(m);

    /* Make sure that this function is not called from the helper thread, unless it is unlocked */
    pa_assert(!m->thread || !pa_thread_is_running(m->thread) || !in_worker(m) || pa_atomic_load(&m->in_once_unlocked));

    pa_mutex_lock(m->mutex);
}

void pa_threaded_mainloop_unlock(pa_threaded_mainloop *m) {
    pa_assert(m);

    /* Make sure that this function is not called from the helper thread, unless it is unlocked */
    pa_assert(!m->thread || !pa_thread_is_running(m->thread) || !in_worker(m) || pa_atomic_load(&m->in_once_unlocked));

    pa_mutex_unlock(m->mutex);
}

/* Called with the lock taken */
void pa_threaded_mainloop_signal(pa_threaded_mainloop *m, int wait_for_accept) {
    pa_assert(m);

    pa_cond_signal(m->cond, 1);

    if (wait_for_accept) {
        m->n_waiting_for_accept ++;

        while (m->n_waiting_for_accept > 0)
            pa_cond_wait(m->accept_cond, m->mutex);
    }
}

/* Called with the lock taken */
void pa_threaded_mainloop_wait(pa_threaded_mainloop *m) {
    pa_assert(m);

    /* Make sure that this function is not called from the helper thread */
    pa_assert(!m->thread || !pa_thread_is_running(m->thread) || !in_worker(m));

    m->n_waiting ++;

    pa_cond_wait(m->cond, m->mutex);

    pa_assert(m->n_waiting > 0);
    m->n_waiting --;
}

/* Called with the lock taken */
void pa_threaded_mainloop_accept(pa_threaded_mainloop *m) {
    pa_assert(m);

    /* Make sure that this function is not called from the helper thread */
    pa_assert(!m->thread || !pa_thread_is_running(m->thread) || !in_worker(m));

    pa_assert(m->n_waiting_for_accept > 0);
    m->n_waiting_for_accept --;

    pa_cond_signal(m->accept_cond, 0);
}

int pa_threaded_mainloop_get_retval(const pa_threaded_mainloop *m) {
    pa_assert(m);

    return pa_mainloop_get_retval(m->real_mainloop);
}

pa_mainloop_api* pa_threaded_mainloop_get_api(pa_threaded_mainloop*m) {
    pa_assert(m);

    return pa_mainloop_get_api(m->real_mainloop);
}

int pa_threaded_mainloop_in_thread(pa_threaded_mainloop *m) {
    pa_assert(m);

    return m->thread && pa_thread_self() == m->thread;
}

void pa_threaded_mainloop_set_name(pa_threaded_mainloop *m, const char *name) {
    pa_assert(m);
    pa_assert(name);

    m->name = pa_xstrdup(name);

    if (m->thread)
        pa_thread_set_name(m->thread, m->name);
}

typedef struct {
    pa_threaded_mainloop *mainloop;
    void (*callback)(pa_threaded_mainloop *m, void *userdata);
    void *userdata;
} once_unlocked_data;

static void once_unlocked_cb(pa_mainloop_api *api, void *userdata) {
    once_unlocked_data *data = userdata;

    pa_assert(userdata);

    pa_atomic_store(&data->mainloop->in_once_unlocked, 1);
    pa_mutex_unlock(data->mainloop->mutex);

    data->callback(data->mainloop, data->userdata);

    pa_mutex_lock(data->mainloop->mutex);
    pa_atomic_store(&data->mainloop->in_once_unlocked, 0);
}

void pa_threaded_mainloop_once_unlocked(pa_threaded_mainloop *m, void (*callback)(pa_threaded_mainloop *m, void *userdata),
        void *userdata) {
    pa_mainloop_api *api;
    once_unlocked_data *data;

    pa_assert(m);
    pa_assert(callback);
    /* Make sure that this function is not called from the helper thread */
    pa_assert((m->thread && !pa_thread_is_running(m->thread)) || !in_worker(m));

    api = pa_mainloop_get_api(m->real_mainloop);
    data = pa_xnew0(once_unlocked_data, 1);

    data->mainloop = m;
    data->callback = callback;
    data->userdata = userdata;

    pa_mainloop_api_once(api, once_unlocked_cb, data);
}
