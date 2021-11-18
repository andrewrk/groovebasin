/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
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
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#ifndef HAVE_PIPE
#include <pulsecore/pipe.h>
#endif

#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/poll.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>
#include <pulsecore/llist.h>
#include <pulsecore/log.h>
#include <pulsecore/core-error.h>
#include <pulsecore/socket.h>
#include <pulsecore/macro.h>

#include "mainloop.h"
#include "internal.h"

struct pa_io_event {
    pa_mainloop *mainloop;
    bool dead:1;

    int fd;
    pa_io_event_flags_t events;
    struct pollfd *pollfd;

    pa_io_event_cb_t callback;
    void *userdata;
    pa_io_event_destroy_cb_t destroy_callback;

    PA_LLIST_FIELDS(pa_io_event);
};

struct pa_time_event {
    pa_mainloop *mainloop;
    bool dead:1;

    bool enabled:1;
    bool use_rtclock:1;
    pa_usec_t time;

    pa_time_event_cb_t callback;
    void *userdata;
    pa_time_event_destroy_cb_t destroy_callback;

    PA_LLIST_FIELDS(pa_time_event);
};

struct pa_defer_event {
    pa_mainloop *mainloop;
    bool dead:1;

    bool enabled:1;

    pa_defer_event_cb_t callback;
    void *userdata;
    pa_defer_event_destroy_cb_t destroy_callback;

    PA_LLIST_FIELDS(pa_defer_event);
};

struct pa_mainloop {
    PA_LLIST_HEAD(pa_io_event, io_events);
    PA_LLIST_HEAD(pa_time_event, time_events);
    PA_LLIST_HEAD(pa_defer_event, defer_events);

    unsigned n_enabled_defer_events, n_enabled_time_events, n_io_events;
    unsigned io_events_please_scan, time_events_please_scan, defer_events_please_scan;

    bool rebuild_pollfds:1;
    struct pollfd *pollfds;
    unsigned max_pollfds, n_pollfds;

    pa_usec_t prepared_timeout;
    pa_time_event *cached_next_time_event;

    pa_mainloop_api api;

    int retval;
    bool quit:1;

    int wakeup_pipe[2];
    int wakeup_pipe_type;

    enum {
        STATE_PASSIVE,
        STATE_PREPARED,
        STATE_POLLING,
        STATE_POLLED,
        STATE_QUIT
    } state;

    pa_poll_func poll_func;
    void *poll_func_userdata;
    int poll_func_ret;
};

static short map_flags_to_libc(pa_io_event_flags_t flags) {
    return (short)
        ((flags & PA_IO_EVENT_INPUT ? POLLIN : 0) |
         (flags & PA_IO_EVENT_OUTPUT ? POLLOUT : 0) |
         (flags & PA_IO_EVENT_ERROR ? POLLERR : 0) |
         (flags & PA_IO_EVENT_HANGUP ? POLLHUP : 0));
}

static pa_io_event_flags_t map_flags_from_libc(short flags) {
    return
        (flags & POLLIN ? PA_IO_EVENT_INPUT : 0) |
        (flags & POLLOUT ? PA_IO_EVENT_OUTPUT : 0) |
        (flags & POLLERR ? PA_IO_EVENT_ERROR : 0) |
        (flags & POLLHUP ? PA_IO_EVENT_HANGUP : 0);
}

/* IO events */
static pa_io_event* mainloop_io_new(
        pa_mainloop_api *a,
        int fd,
        pa_io_event_flags_t events,
        pa_io_event_cb_t callback,
        void *userdata) {

    pa_mainloop *m;
    pa_io_event *e;

    pa_assert(a);
    pa_assert(a->userdata);
    pa_assert(fd >= 0);
    pa_assert(callback);

    m = a->userdata;
    pa_assert(a == &m->api);

    e = pa_xnew0(pa_io_event, 1);
    e->mainloop = m;

    e->fd = fd;
    e->events = events;

    e->callback = callback;
    e->userdata = userdata;

    PA_LLIST_PREPEND(pa_io_event, m->io_events, e);
    m->rebuild_pollfds = true;
    m->n_io_events ++;

    pa_mainloop_wakeup(m);

    return e;
}

static void mainloop_io_enable(pa_io_event *e, pa_io_event_flags_t events) {
    pa_assert(e);
    pa_assert(!e->dead);

    if (e->events == events)
        return;

    e->events = events;

    if (e->pollfd)
        e->pollfd->events = map_flags_to_libc(events);
    else
        e->mainloop->rebuild_pollfds = true;

    pa_mainloop_wakeup(e->mainloop);
}

static void mainloop_io_free(pa_io_event *e) {
    pa_assert(e);
    pa_assert(!e->dead);

    e->dead = true;
    e->mainloop->io_events_please_scan ++;

    e->mainloop->n_io_events --;
    e->mainloop->rebuild_pollfds = true;

    pa_mainloop_wakeup(e->mainloop);
}

static void mainloop_io_set_destroy(pa_io_event *e, pa_io_event_destroy_cb_t callback) {
    pa_assert(e);

    e->destroy_callback = callback;
}

/* Defer events */
static pa_defer_event* mainloop_defer_new(
        pa_mainloop_api *a,
        pa_defer_event_cb_t callback,
        void *userdata) {

    pa_mainloop *m;
    pa_defer_event *e;

    pa_assert(a);
    pa_assert(a->userdata);
    pa_assert(callback);

    m = a->userdata;
    pa_assert(a == &m->api);

    e = pa_xnew0(pa_defer_event, 1);
    e->mainloop = m;

    e->enabled = true;
    m->n_enabled_defer_events++;

    e->callback = callback;
    e->userdata = userdata;

    PA_LLIST_PREPEND(pa_defer_event, m->defer_events, e);

    pa_mainloop_wakeup(e->mainloop);

    return e;
}

static void mainloop_defer_enable(pa_defer_event *e, int b) {
    pa_assert(e);
    pa_assert(!e->dead);

    if (e->enabled && !b) {
        pa_assert(e->mainloop->n_enabled_defer_events > 0);
        e->mainloop->n_enabled_defer_events--;
    } else if (!e->enabled && b) {
        e->mainloop->n_enabled_defer_events++;
        pa_mainloop_wakeup(e->mainloop);
    }

    e->enabled = b;
}

static void mainloop_defer_free(pa_defer_event *e) {
    pa_assert(e);
    pa_assert(!e->dead);

    e->dead = true;
    e->mainloop->defer_events_please_scan ++;

    if (e->enabled) {
        pa_assert(e->mainloop->n_enabled_defer_events > 0);
        e->mainloop->n_enabled_defer_events--;
        e->enabled = false;
    }
}

static void mainloop_defer_set_destroy(pa_defer_event *e, pa_defer_event_destroy_cb_t callback) {
    pa_assert(e);
    pa_assert(!e->dead);

    e->destroy_callback = callback;
}

/* Time events */
static pa_usec_t make_rt(const struct timeval *tv, bool *use_rtclock) {
    struct timeval ttv;

    if (!tv) {
        *use_rtclock = false;
        return PA_USEC_INVALID;
    }

    ttv = *tv;
    *use_rtclock = !!(ttv.tv_usec & PA_TIMEVAL_RTCLOCK);

    if (*use_rtclock)
        ttv.tv_usec &= ~PA_TIMEVAL_RTCLOCK;
    else
        pa_rtclock_from_wallclock(&ttv);

    return pa_timeval_load(&ttv);
}

static pa_time_event* mainloop_time_new(
        pa_mainloop_api *a,
        const struct timeval *tv,
        pa_time_event_cb_t callback,
        void *userdata) {

    pa_mainloop *m;
    pa_time_event *e;
    pa_usec_t t;
    bool use_rtclock = false;

    pa_assert(a);
    pa_assert(a->userdata);
    pa_assert(callback);

    t = make_rt(tv, &use_rtclock);

    m = a->userdata;
    pa_assert(a == &m->api);

    e = pa_xnew0(pa_time_event, 1);
    e->mainloop = m;

    if ((e->enabled = (t != PA_USEC_INVALID))) {
        e->time = t;
        e->use_rtclock = use_rtclock;

        m->n_enabled_time_events++;

        if (m->cached_next_time_event) {
            pa_assert(m->cached_next_time_event->enabled);

            if (t < m->cached_next_time_event->time)
                m->cached_next_time_event = e;
        }
    }

    e->callback = callback;
    e->userdata = userdata;

    PA_LLIST_PREPEND(pa_time_event, m->time_events, e);

    if (e->enabled)
        pa_mainloop_wakeup(m);

    return e;
}

static void mainloop_time_restart(pa_time_event *e, const struct timeval *tv) {
    bool valid;
    pa_usec_t t;
    bool use_rtclock = false;

    pa_assert(e);
    pa_assert(!e->dead);

    t = make_rt(tv, &use_rtclock);

    valid = (t != PA_USEC_INVALID);
    if (e->enabled && !valid) {
        pa_assert(e->mainloop->n_enabled_time_events > 0);
        e->mainloop->n_enabled_time_events--;
    } else if (!e->enabled && valid)
        e->mainloop->n_enabled_time_events++;

    if ((e->enabled = valid)) {
        e->time = t;
        e->use_rtclock = use_rtclock;
        pa_mainloop_wakeup(e->mainloop);
    }

    if (e->mainloop->cached_next_time_event == e)
        e->mainloop->cached_next_time_event = NULL;

    if (e->mainloop->cached_next_time_event && e->enabled) {
        pa_assert(e->mainloop->cached_next_time_event->enabled);

        if (t < e->mainloop->cached_next_time_event->time)
            e->mainloop->cached_next_time_event = e;
    }
}

static void mainloop_time_free(pa_time_event *e) {
    pa_assert(e);
    pa_assert(!e->dead);

    e->dead = true;
    e->mainloop->time_events_please_scan ++;

    if (e->enabled) {
        pa_assert(e->mainloop->n_enabled_time_events > 0);
        e->mainloop->n_enabled_time_events--;
        e->enabled = false;
    }

    if (e->mainloop->cached_next_time_event == e)
        e->mainloop->cached_next_time_event = NULL;

    /* no wakeup needed here. Think about it! */
}

static void mainloop_time_set_destroy(pa_time_event *e, pa_time_event_destroy_cb_t callback) {
    pa_assert(e);
    pa_assert(!e->dead);

    e->destroy_callback = callback;
}

/* quit() */

static void mainloop_quit(pa_mainloop_api *a, int retval) {
    pa_mainloop *m;

    pa_assert(a);
    pa_assert(a->userdata);
    m = a->userdata;
    pa_assert(a == &m->api);

    pa_mainloop_quit(m, retval);
}

static const pa_mainloop_api vtable = {
    .userdata = NULL,

    .io_new = mainloop_io_new,
    .io_enable = mainloop_io_enable,
    .io_free = mainloop_io_free,
    .io_set_destroy = mainloop_io_set_destroy,

    .time_new = mainloop_time_new,
    .time_restart = mainloop_time_restart,
    .time_free = mainloop_time_free,
    .time_set_destroy = mainloop_time_set_destroy,

    .defer_new = mainloop_defer_new,
    .defer_enable = mainloop_defer_enable,
    .defer_free = mainloop_defer_free,
    .defer_set_destroy = mainloop_defer_set_destroy,

    .quit = mainloop_quit,
};

pa_mainloop *pa_mainloop_new(void) {
    pa_mainloop *m;

    pa_init_i18n();

    m = pa_xnew0(pa_mainloop, 1);

    if (pa_pipe_cloexec(m->wakeup_pipe) < 0) {
        pa_log_error("ERROR: cannot create wakeup pipe");
        pa_xfree(m);
        return NULL;
    }

    pa_make_fd_nonblock(m->wakeup_pipe[0]);
    pa_make_fd_nonblock(m->wakeup_pipe[1]);

    m->rebuild_pollfds = true;

    m->api = vtable;
    m->api.userdata = m;

    m->state = STATE_PASSIVE;

    m->poll_func_ret = -1;

    return m;
}

static void cleanup_io_events(pa_mainloop *m, bool force) {
    pa_io_event *e, *n;

    PA_LLIST_FOREACH_SAFE(e, n, m->io_events) {

        if (!force && m->io_events_please_scan <= 0)
            break;

        if (force || e->dead) {
            PA_LLIST_REMOVE(pa_io_event, m->io_events, e);

            if (e->dead) {
                pa_assert(m->io_events_please_scan > 0);
                m->io_events_please_scan--;
            }

            if (e->destroy_callback)
                e->destroy_callback(&m->api, e, e->userdata);

            pa_xfree(e);

            m->rebuild_pollfds = true;
        }
    }

    pa_assert(m->io_events_please_scan == 0);
}

static void cleanup_time_events(pa_mainloop *m, bool force) {
    pa_time_event *e, *n;

    PA_LLIST_FOREACH_SAFE(e, n, m->time_events) {

        if (!force && m->time_events_please_scan <= 0)
            break;

        if (force || e->dead) {
            PA_LLIST_REMOVE(pa_time_event, m->time_events, e);

            if (e->dead) {
                pa_assert(m->time_events_please_scan > 0);
                m->time_events_please_scan--;
            }

            if (!e->dead && e->enabled) {
                pa_assert(m->n_enabled_time_events > 0);
                m->n_enabled_time_events--;
                e->enabled = false;
            }

            if (e->destroy_callback)
                e->destroy_callback(&m->api, e, e->userdata);

            pa_xfree(e);
        }
    }

    pa_assert(m->time_events_please_scan == 0);
}

static void cleanup_defer_events(pa_mainloop *m, bool force) {
    pa_defer_event *e, *n;

    PA_LLIST_FOREACH_SAFE(e, n, m->defer_events) {

        if (!force && m->defer_events_please_scan <= 0)
            break;

        if (force || e->dead) {
            PA_LLIST_REMOVE(pa_defer_event, m->defer_events, e);

            if (e->dead) {
                pa_assert(m->defer_events_please_scan > 0);
                m->defer_events_please_scan--;
            }

            if (!e->dead && e->enabled) {
                pa_assert(m->n_enabled_defer_events > 0);
                m->n_enabled_defer_events--;
                e->enabled = false;
            }

            if (e->destroy_callback)
                e->destroy_callback(&m->api, e, e->userdata);

            pa_xfree(e);
        }
    }

    pa_assert(m->defer_events_please_scan == 0);
}

void pa_mainloop_free(pa_mainloop *m) {
    pa_assert(m);

    cleanup_io_events(m, true);
    cleanup_defer_events(m, true);
    cleanup_time_events(m, true);

    pa_xfree(m->pollfds);

    pa_close_pipe(m->wakeup_pipe);

    pa_xfree(m);
}

static void scan_dead(pa_mainloop *m) {
    pa_assert(m);

    if (m->io_events_please_scan)
        cleanup_io_events(m, false);

    if (m->time_events_please_scan)
        cleanup_time_events(m, false);

    if (m->defer_events_please_scan)
        cleanup_defer_events(m, false);
}

static void rebuild_pollfds(pa_mainloop *m) {
    pa_io_event*e;
    struct pollfd *p;
    unsigned l;

    l = m->n_io_events + 1;
    if (m->max_pollfds < l) {
        l *= 2;
        m->pollfds = pa_xrealloc(m->pollfds, sizeof(struct pollfd)*l);
        m->max_pollfds = l;
    }

    m->n_pollfds = 0;
    p = m->pollfds;

    m->pollfds[0].fd = m->wakeup_pipe[0];
    m->pollfds[0].events = POLLIN;
    m->pollfds[0].revents = 0;
    p++;
    m->n_pollfds++;

    PA_LLIST_FOREACH(e, m->io_events) {
        if (e->dead) {
            e->pollfd = NULL;
            continue;
        }

        e->pollfd = p;
        p->fd = e->fd;
        p->events = map_flags_to_libc(e->events);
        p->revents = 0;

        p++;
        m->n_pollfds++;
    }

    m->rebuild_pollfds = false;
}

static unsigned dispatch_pollfds(pa_mainloop *m) {
    pa_io_event *e;
    unsigned r = 0, k;

    pa_assert(m->poll_func_ret > 0);

    k = m->poll_func_ret;

    PA_LLIST_FOREACH(e, m->io_events) {

        if (k <= 0 || m->quit)
            break;

        if (e->dead || !e->pollfd || !e->pollfd->revents)
            continue;

        pa_assert(e->pollfd->fd == e->fd);
        pa_assert(e->callback);

        e->callback(&m->api, e, e->fd, map_flags_from_libc(e->pollfd->revents), e->userdata);
        e->pollfd->revents = 0;
        r++;
        k--;
    }

    return r;
}

static unsigned dispatch_defer(pa_mainloop *m) {
    pa_defer_event *e;
    unsigned r = 0;

    if (m->n_enabled_defer_events <= 0)
        return 0;

    PA_LLIST_FOREACH(e, m->defer_events) {

        if (m->quit)
            break;

        if (e->dead || !e->enabled)
            continue;

        pa_assert(e->callback);
        e->callback(&m->api, e, e->userdata);
        r++;
    }

    return r;
}

static pa_time_event* find_next_time_event(pa_mainloop *m) {
    pa_time_event *t, *n = NULL;
    pa_assert(m);

    if (m->cached_next_time_event)
        return m->cached_next_time_event;

    PA_LLIST_FOREACH(t, m->time_events) {

        if (t->dead || !t->enabled)
            continue;

        if (!n || t->time < n->time) {
            n = t;

            /* Shortcut for time == 0 */
            if (n->time == 0)
                break;
        }
    }

    m->cached_next_time_event = n;
    return n;
}

static pa_usec_t calc_next_timeout(pa_mainloop *m) {
    pa_time_event *t;
    pa_usec_t clock_now;

    if (m->n_enabled_time_events <= 0)
        return PA_USEC_INVALID;

    pa_assert_se(t = find_next_time_event(m));

    if (t->time <= 0)
        return 0;

    clock_now = pa_rtclock_now();

    if (t->time <= clock_now)
        return 0;

    return t->time - clock_now;
}

static unsigned dispatch_timeout(pa_mainloop *m) {
    pa_time_event *e;
    pa_usec_t now;
    unsigned r = 0;
    pa_assert(m);

    if (m->n_enabled_time_events <= 0)
        return 0;

    now = pa_rtclock_now();

    PA_LLIST_FOREACH(e, m->time_events) {

        if (m->quit)
            break;

        if (e->dead || !e->enabled)
            continue;

        if (e->time <= now) {
            struct timeval tv;
            pa_assert(e->callback);

            /* Disable time event */
            mainloop_time_restart(e, NULL);

            e->callback(&m->api, e, pa_timeval_rtstore(&tv, e->time, e->use_rtclock), e->userdata);

            r++;
        }
    }

    return r;
}

void pa_mainloop_wakeup(pa_mainloop *m) {
    char c = 'W';
    pa_assert(m);

    if (pa_write(m->wakeup_pipe[1], &c, sizeof(c), &m->wakeup_pipe_type) < 0)
        /* Not many options for recovering from the error. Let's at least log something. */
        pa_log("pa_write() failed while trying to wake up the mainloop: %s", pa_cstrerror(errno));
}

static void clear_wakeup(pa_mainloop *m) {
    char c[10];

    pa_assert(m);

    while (pa_read(m->wakeup_pipe[0], &c, sizeof(c), &m->wakeup_pipe_type) == sizeof(c))
        ;
}

int pa_mainloop_prepare(pa_mainloop *m, int timeout) {
    pa_assert(m);
    pa_assert(m->state == STATE_PASSIVE);

    clear_wakeup(m);
    scan_dead(m);

    if (m->quit)
        goto quit;

    if (m->n_enabled_defer_events <= 0) {

        if (m->rebuild_pollfds)
            rebuild_pollfds(m);

        m->prepared_timeout = calc_next_timeout(m);
        if (timeout >= 0) {
            if (timeout < m->prepared_timeout || m->prepared_timeout == PA_USEC_INVALID)
                m->prepared_timeout = timeout;
        }
    }

    m->state = STATE_PREPARED;
    return 0;

quit:
    m->state = STATE_QUIT;
    return -2;
}

static int usec_to_timeout(pa_usec_t u) {
    int timeout;

    if (u == PA_USEC_INVALID)
        return -1;

    timeout = (u + PA_USEC_PER_MSEC - 1) / PA_USEC_PER_MSEC;
    pa_assert(timeout >= 0);

    return timeout;
}

int pa_mainloop_poll(pa_mainloop *m) {
    pa_assert(m);
    pa_assert(m->state == STATE_PREPARED);

    if (m->quit)
        goto quit;

    m->state = STATE_POLLING;

    if (m->n_enabled_defer_events)
        m->poll_func_ret = 0;
    else {
        pa_assert(!m->rebuild_pollfds);

        if (m->poll_func)
            m->poll_func_ret = m->poll_func(
                    m->pollfds, m->n_pollfds,
                    usec_to_timeout(m->prepared_timeout),
                    m->poll_func_userdata);
        else {
#ifdef HAVE_PPOLL
            struct timespec ts;

            m->poll_func_ret = ppoll(
                    m->pollfds, m->n_pollfds,
                    m->prepared_timeout == PA_USEC_INVALID ? NULL : pa_timespec_store(&ts, m->prepared_timeout),
                    NULL);
#else
            m->poll_func_ret = pa_poll(
                    m->pollfds, m->n_pollfds,
                    usec_to_timeout(m->prepared_timeout));
#endif
        }

        if (m->poll_func_ret < 0) {
            if (errno == EINTR)
                m->poll_func_ret = 0;
            else
                pa_log("poll(): %s", pa_cstrerror(errno));
        }
    }

    m->state = m->poll_func_ret < 0 ? STATE_PASSIVE : STATE_POLLED;
    return m->poll_func_ret;

quit:
    m->state = STATE_QUIT;
    return -2;
}

int pa_mainloop_dispatch(pa_mainloop *m) {
    unsigned dispatched = 0;

    pa_assert(m);
    pa_assert(m->state == STATE_POLLED);

    if (m->quit)
        goto quit;

    if (m->n_enabled_defer_events)
        dispatched += dispatch_defer(m);
    else {
        if (m->n_enabled_time_events)
            dispatched += dispatch_timeout(m);

        if (m->quit)
            goto quit;

        if (m->poll_func_ret > 0)
            dispatched += dispatch_pollfds(m);
    }

    if (m->quit)
        goto quit;

    m->state = STATE_PASSIVE;

    return (int) dispatched;

quit:
    m->state = STATE_QUIT;
    return -2;
}

int pa_mainloop_get_retval(const pa_mainloop *m) {
    pa_assert(m);

    return m->retval;
}

int pa_mainloop_iterate(pa_mainloop *m, int block, int *retval) {
    int r;
    pa_assert(m);

    if ((r = pa_mainloop_prepare(m, block ? -1 : 0)) < 0)
        goto quit;

    if ((r = pa_mainloop_poll(m)) < 0)
        goto quit;

    if ((r = pa_mainloop_dispatch(m)) < 0)
        goto quit;

    return r;

quit:

    if ((r == -2) && retval)
        *retval = pa_mainloop_get_retval(m);
    return r;
}

int pa_mainloop_run(pa_mainloop *m, int *retval) {
    int r;

    while ((r = pa_mainloop_iterate(m, 1, retval)) >= 0)
        ;

    if (r == -2)
        return 1;
    else
        return -1;
}

void pa_mainloop_quit(pa_mainloop *m, int retval) {
    pa_assert(m);

    m->quit = true;
    m->retval = retval;
    pa_mainloop_wakeup(m);
}

pa_mainloop_api* pa_mainloop_get_api(pa_mainloop *m) {
    pa_assert(m);

    return &m->api;
}

void pa_mainloop_set_poll_func(pa_mainloop *m, pa_poll_func poll_func, void *userdata) {
    pa_assert(m);

    m->poll_func = poll_func;
    m->poll_func_userdata = userdata;
}

bool pa_mainloop_is_our_api(const pa_mainloop_api *m) {
    pa_assert(m);

    return m->io_new == mainloop_io_new;
}
