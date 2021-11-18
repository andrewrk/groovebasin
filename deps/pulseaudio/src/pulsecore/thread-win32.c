/***
  This file is part of PulseAudio.

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

#include <stdio.h>

#include <windows.h>

#include <pulse/xmalloc.h>
#include <pulsecore/once.h>

#include "thread.h"

struct pa_thread {
    HANDLE thread;
    pa_thread_func_t thread_func;
    void *userdata;
};

struct pa_tls {
    DWORD index;
    pa_free_cb_t free_func;
};

struct pa_tls_monitor {
    HANDLE thread;
    pa_free_cb_t free_func;
    void *data;
};

static pa_tls *thread_tls;
static pa_once thread_tls_once = PA_ONCE_INIT;
static pa_tls *monitor_tls;

static void thread_tls_once_func(void) {
    thread_tls = pa_tls_new(NULL);
    assert(thread_tls);
}

static DWORD WINAPI internal_thread_func(LPVOID param) {
    pa_thread *t = param;
    assert(t);

    pa_run_once(&thread_tls_once, thread_tls_once_func);
    pa_tls_set(thread_tls, t);

    t->thread_func(t->userdata);

    return 0;
}

pa_thread* pa_thread_new(const char *name, pa_thread_func_t thread_func, void *userdata) {
    pa_thread *t;
    DWORD thread_id;

    assert(thread_func);

    t = pa_xnew(pa_thread, 1);
    t->thread_func = thread_func;
    t->userdata = userdata;

    t->thread = CreateThread(NULL, 0, internal_thread_func, t, 0, &thread_id);

    if (!t->thread) {
        pa_xfree(t);
        return NULL;
    }

    return t;
}

int pa_thread_is_running(pa_thread *t) {
    DWORD code;

    assert(t);

    if (!GetExitCodeThread(t->thread, &code))
        return 0;

    return code == STILL_ACTIVE;
}

void pa_thread_free(pa_thread *t) {
    assert(t);

    pa_thread_join(t);
    CloseHandle(t->thread);
    pa_xfree(t);
}

void pa_thread_free_nojoin(pa_thread *t) {
    pa_assert(t);

    CloseHandle(t->thread);
    pa_xfree(t);
}

int pa_thread_join(pa_thread *t) {
    assert(t);

    if (WaitForSingleObject(t->thread, INFINITE) == WAIT_FAILED)
        return -1;

    return 0;
}

pa_thread* pa_thread_self(void) {
    pa_run_once(&thread_tls_once, thread_tls_once_func);
    return pa_tls_get(thread_tls);
}

void* pa_thread_get_data(pa_thread *t) {
    pa_assert(t);

    return t->userdata;
}

void pa_thread_set_data(pa_thread *t, void *userdata) {
    pa_assert(t);

    t->userdata = userdata;
}

void pa_thread_set_name(pa_thread *t, const char *name) {
    /* Not implemented */
}

const char *pa_thread_get_name(pa_thread *t) {
    /* Not implemented */
    return NULL;
}

void pa_thread_yield(void) {
    Sleep(0);
}

static DWORD WINAPI monitor_thread_func(LPVOID param) {
    struct pa_tls_monitor *m = param;
    assert(m);

    WaitForSingleObject(m->thread, INFINITE);

    CloseHandle(m->thread);

    m->free_func(m->data);

    pa_xfree(m);

    return 0;
}

pa_tls* pa_tls_new(pa_free_cb_t free_cb) {
    pa_tls *t;

    t = pa_xnew(pa_tls, 1);
    t->index = TlsAlloc();
    t->free_func = free_cb;

    if (t->index == TLS_OUT_OF_INDEXES) {
        pa_xfree(t);
        return NULL;
    }

    return t;
}

void pa_tls_free(pa_tls *t) {
    assert(t);

    TlsFree(t->index);
    pa_xfree(t);
}

void *pa_tls_get(pa_tls *t) {
    assert(t);

    return TlsGetValue(t->index);
}

void *pa_tls_set(pa_tls *t, void *userdata) {
    void *r;

    assert(t);

    r = TlsGetValue(t->index);

    TlsSetValue(t->index, userdata);

    if (t->free_func) {
        struct pa_tls_monitor *m;

        PA_ONCE_BEGIN {
            monitor_tls = pa_tls_new(NULL);
            assert(monitor_tls);
            pa_tls_set(monitor_tls, NULL);
        } PA_ONCE_END;

        m = pa_tls_get(monitor_tls);
        if (!m) {
            HANDLE thread;

            m = pa_xnew(struct pa_tls_monitor, 1);

            DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                GetCurrentProcess(), &m->thread, 0, FALSE,
                DUPLICATE_SAME_ACCESS);

            m->free_func = t->free_func;

            pa_tls_set(monitor_tls, m);

            thread = CreateThread(NULL, 0, monitor_thread_func, m, 0, NULL);
            assert(thread);
            CloseHandle(thread);
        }

        m->data = userdata;
    }

    return r;
}
