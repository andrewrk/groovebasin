/***
  This file is part of PulseAudio.

  Copyright 2004-2008 Lennart Poettering
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
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "mainloop-signal.h"

struct pa_signal_event {
    int sig;
#ifdef HAVE_SIGACTION
    struct sigaction saved_sigaction;
#else
    void (*saved_handler)(int sig);
#endif
    void *userdata;
    pa_signal_cb_t callback;
    pa_signal_destroy_cb_t destroy_callback;
    pa_signal_event *previous, *next;
};

static pa_mainloop_api *api = NULL;
static int signal_pipe[2] = { -1, -1 };
static pa_io_event* io_event = NULL;
static pa_signal_event *signals = NULL;

static void signal_handler(int sig) {
    int saved_errno;

    saved_errno = errno;

#ifndef HAVE_SIGACTION
    signal(sig, signal_handler);
#endif

    /* XXX: If writing fails, there's nothing we can do? */
    (void) pa_write(signal_pipe[1], &sig, sizeof(sig), NULL);

    errno = saved_errno;
}

static void dispatch(pa_mainloop_api*a, int sig) {
    pa_signal_event *s;

    for (s = signals; s; s = s->next)
        if (s->sig == sig) {
            pa_assert(s->callback);
            s->callback(a, s, sig, s->userdata);
            break;
        }
}

static void callback(pa_mainloop_api*a, pa_io_event*e, int fd, pa_io_event_flags_t f, void *userdata) {
    ssize_t r;
    int sig;

    pa_assert(a);
    pa_assert(e);
    pa_assert(f == PA_IO_EVENT_INPUT);
    pa_assert(e == io_event);
    pa_assert(fd == signal_pipe[0]);

    if ((r = pa_read(signal_pipe[0], &sig, sizeof(sig), NULL)) < 0) {
        if (errno == EAGAIN)
            return;

        pa_log("read(): %s", pa_cstrerror(errno));
        return;
    }

    if (r != sizeof(sig)) {
        pa_log("short read()");
        return;
    }

    dispatch(a, sig);
}

int pa_signal_init(pa_mainloop_api *a) {

    pa_assert(a);
    pa_assert(!api);
    pa_assert(signal_pipe[0] == -1);
    pa_assert(signal_pipe[1] == -1);
    pa_assert(!io_event);

    if (pa_pipe_cloexec(signal_pipe) < 0) {
        pa_log("pipe(): %s", pa_cstrerror(errno));
        return -1;
    }

    pa_make_fd_nonblock(signal_pipe[0]);
    pa_make_fd_nonblock(signal_pipe[1]);

    api = a;

    pa_assert_se(io_event = api->io_new(api, signal_pipe[0], PA_IO_EVENT_INPUT, callback, NULL));

    return 0;
}

void pa_signal_done(void) {
    while (signals)
        pa_signal_free(signals);

    if (io_event) {
        pa_assert(api);
        api->io_free(io_event);
        io_event = NULL;
    }

    pa_close_pipe(signal_pipe);

    api = NULL;
}

pa_signal_event* pa_signal_new(int sig, pa_signal_cb_t _callback, void *userdata) {
    pa_signal_event *e = NULL;

#ifdef HAVE_SIGACTION
    struct sigaction sa;
#endif

    pa_assert(sig > 0);
    pa_assert(_callback);

    pa_init_i18n();

    for (e = signals; e; e = e->next)
        if (e->sig == sig)
            return NULL;

    e = pa_xnew(pa_signal_event, 1);
    e->sig = sig;
    e->callback = _callback;
    e->userdata = userdata;
    e->destroy_callback = NULL;

#ifdef HAVE_SIGACTION
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(sig, &sa, &e->saved_sigaction) < 0)
#else
    if ((e->saved_handler = signal(sig, signal_handler)) == SIG_ERR)
#endif
        goto fail;

    e->previous = NULL;
    e->next = signals;
    signals = e;

    return e;
fail:
    pa_xfree(e);
    return NULL;
}

void pa_signal_free(pa_signal_event *e) {
    pa_assert(e);

    if (e->next)
        e->next->previous = e->previous;
    if (e->previous)
        e->previous->next = e->next;
    else
        signals = e->next;

#ifdef HAVE_SIGACTION
    pa_assert_se(sigaction(e->sig, &e->saved_sigaction, NULL) == 0);
#else
    pa_assert_se(signal(e->sig, e->saved_handler) == signal_handler);
#endif

    if (e->destroy_callback)
        e->destroy_callback(api, e, e->userdata);

    pa_xfree(e);
}

void pa_signal_set_destroy(pa_signal_event *e, pa_signal_destroy_cb_t _callback) {
    pa_assert(e);

    e->destroy_callback = _callback;
}
