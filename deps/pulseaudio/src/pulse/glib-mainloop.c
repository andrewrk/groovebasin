/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <pulse/xmalloc.h>
#include <pulse/timeval.h>

#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/llist.h>

#include <glib.h>
#include "glib-mainloop.h"

struct pa_io_event {
    pa_glib_mainloop *mainloop;
    int dead;

    GPollFD poll_fd;
    int poll_fd_added;

    pa_io_event_cb_t callback;
    void *userdata;
    pa_io_event_destroy_cb_t destroy_callback;

    PA_LLIST_FIELDS(pa_io_event);
};

struct pa_time_event {
    pa_glib_mainloop *mainloop;
    int dead;

    int enabled;
    struct timeval timeval;

    pa_time_event_cb_t callback;
    void *userdata;
    pa_time_event_destroy_cb_t destroy_callback;

    PA_LLIST_FIELDS(pa_time_event);
};

struct pa_defer_event {
    pa_glib_mainloop *mainloop;
    int dead;

    int enabled;

    pa_defer_event_cb_t callback;
    void *userdata;
    pa_defer_event_destroy_cb_t destroy_callback;

    PA_LLIST_FIELDS(pa_defer_event);
};

struct pa_glib_mainloop {
    GSource source;

    pa_mainloop_api api;
    GMainContext *context;

    PA_LLIST_HEAD(pa_io_event, io_events);
    PA_LLIST_HEAD(pa_time_event, time_events);
    PA_LLIST_HEAD(pa_defer_event, defer_events);

    int n_enabled_defer_events, n_enabled_time_events;
    int io_events_please_scan, time_events_please_scan, defer_events_please_scan;

    pa_time_event *cached_next_time_event;
};

static void cleanup_io_events(pa_glib_mainloop *g, int force) {
    pa_io_event *e;

    e = g->io_events;
    while (e) {
        pa_io_event *n = e->next;

        if (!force && g->io_events_please_scan <= 0)
            break;

        if (force || e->dead) {
            PA_LLIST_REMOVE(pa_io_event, g->io_events, e);

            if (e->dead) {
                g_assert(g->io_events_please_scan > 0);
                g->io_events_please_scan--;
            }

            if (e->poll_fd_added)
                g_source_remove_poll(&g->source, &e->poll_fd);

            if (e->destroy_callback)
                e->destroy_callback(&g->api, e, e->userdata);

            pa_xfree(e);
        }

        e = n;
    }

    g_assert(g->io_events_please_scan == 0);
}

static void cleanup_time_events(pa_glib_mainloop *g, int force) {
    pa_time_event *e;

    e = g->time_events;
    while (e) {
        pa_time_event *n = e->next;

        if (!force && g->time_events_please_scan <= 0)
            break;

        if (force || e->dead) {
            PA_LLIST_REMOVE(pa_time_event, g->time_events, e);

            if (e->dead) {
                g_assert(g->time_events_please_scan > 0);
                g->time_events_please_scan--;
            }

            if (!e->dead && e->enabled) {
                g_assert(g->n_enabled_time_events > 0);
                g->n_enabled_time_events--;
            }

            if (e->destroy_callback)
                e->destroy_callback(&g->api, e, e->userdata);

            pa_xfree(e);
        }

        e = n;
    }

    g_assert(g->time_events_please_scan == 0);
}

static void cleanup_defer_events(pa_glib_mainloop *g, int force) {
    pa_defer_event *e;

    e = g->defer_events;
    while (e) {
        pa_defer_event *n = e->next;

        if (!force && g->defer_events_please_scan <= 0)
            break;

        if (force || e->dead) {
            PA_LLIST_REMOVE(pa_defer_event, g->defer_events, e);

            if (e->dead) {
                g_assert(g->defer_events_please_scan > 0);
                g->defer_events_please_scan--;
            }

            if (!e->dead && e->enabled) {
                g_assert(g->n_enabled_defer_events > 0);
                g->n_enabled_defer_events--;
            }

            if (e->destroy_callback)
                e->destroy_callback(&g->api, e, e->userdata);

            pa_xfree(e);
        }

        e = n;
    }

    g_assert(g->defer_events_please_scan == 0);
}

static gushort map_flags_to_glib(pa_io_event_flags_t flags) {
    return (gushort)
        ((flags & PA_IO_EVENT_INPUT ? G_IO_IN : 0) |
         (flags & PA_IO_EVENT_OUTPUT ? G_IO_OUT : 0) |
         (flags & PA_IO_EVENT_ERROR ? G_IO_ERR : 0) |
         (flags & PA_IO_EVENT_HANGUP ? G_IO_HUP : 0));
}

static pa_io_event_flags_t map_flags_from_glib(gushort flags) {
    return
        (flags & G_IO_IN ? PA_IO_EVENT_INPUT : 0) |
        (flags & G_IO_OUT ? PA_IO_EVENT_OUTPUT : 0) |
        (flags & G_IO_ERR ? PA_IO_EVENT_ERROR : 0) |
        (flags & G_IO_HUP ? PA_IO_EVENT_HANGUP : 0);
}

static pa_io_event* glib_io_new(
        pa_mainloop_api*m,
        int fd,
        pa_io_event_flags_t f,
        pa_io_event_cb_t cb,
        void *userdata) {

    pa_io_event *e;
    pa_glib_mainloop *g;

    g_assert(m);
    g_assert(m->userdata);
    g_assert(fd >= 0);
    g_assert(cb);

    g = m->userdata;

    e = pa_xnew(pa_io_event, 1);
    e->mainloop = g;
    e->dead = 0;

    e->poll_fd.fd = fd;
    e->poll_fd.events = map_flags_to_glib(f);
    e->poll_fd.revents = 0;

    e->callback = cb;
    e->userdata = userdata;
    e->destroy_callback = NULL;

    PA_LLIST_PREPEND(pa_io_event, g->io_events, e);

    g_source_add_poll(&g->source, &e->poll_fd);
    e->poll_fd_added = 1;

    return e;
}

static void glib_io_enable(pa_io_event*e, pa_io_event_flags_t f) {
    g_assert(e);
    g_assert(!e->dead);

    e->poll_fd.events = map_flags_to_glib(f);
}

static void glib_io_free(pa_io_event*e) {
    g_assert(e);
    g_assert(!e->dead);

    e->dead = 1;
    e->mainloop->io_events_please_scan++;

    if (e->poll_fd_added) {
        g_source_remove_poll(&e->mainloop->source, &e->poll_fd);
        e->poll_fd_added = 0;
    }
}

static void glib_io_set_destroy(pa_io_event*e, pa_io_event_destroy_cb_t cb) {
    g_assert(e);
    g_assert(!e->dead);

    e->destroy_callback = cb;
}

/* Time sources */

static pa_time_event* glib_time_new(
        pa_mainloop_api*m,
        const struct timeval *tv,
        pa_time_event_cb_t cb,
        void *userdata) {

    pa_glib_mainloop *g;
    pa_time_event *e;

    g_assert(m);
    g_assert(m->userdata);
    g_assert(cb);

    g = m->userdata;

    e = pa_xnew(pa_time_event, 1);
    e->mainloop = g;
    e->dead = 0;

    if ((e->enabled = !!tv)) {
        e->timeval = *tv;
        g->n_enabled_time_events++;

        if (g->cached_next_time_event) {
            g_assert(g->cached_next_time_event->enabled);

            if (pa_timeval_cmp(tv, &g->cached_next_time_event->timeval) < 0)
                g->cached_next_time_event = e;
        }
    }

    e->callback = cb;
    e->userdata = userdata;
    e->destroy_callback = NULL;

    PA_LLIST_PREPEND(pa_time_event, g->time_events, e);

    return e;
}

static void glib_time_restart(pa_time_event*e, const struct timeval *tv) {
    g_assert(e);
    g_assert(!e->dead);

    if (e->enabled && !tv) {
        g_assert(e->mainloop->n_enabled_time_events > 0);
        e->mainloop->n_enabled_time_events--;
    } else if (!e->enabled && tv)
        e->mainloop->n_enabled_time_events++;

    if ((e->enabled = !!tv))
        e->timeval = *tv;

    if (e->mainloop->cached_next_time_event == e)
        e->mainloop->cached_next_time_event = NULL;

    if (e->mainloop->cached_next_time_event && e->enabled) {
        g_assert(e->mainloop->cached_next_time_event->enabled);

        if (pa_timeval_cmp(tv, &e->mainloop->cached_next_time_event->timeval) < 0)
            e->mainloop->cached_next_time_event = e;
    }
}

static void glib_time_free(pa_time_event *e) {
    g_assert(e);
    g_assert(!e->dead);

    e->dead = 1;
    e->mainloop->time_events_please_scan++;

    if (e->enabled)
        e->mainloop->n_enabled_time_events--;

    if (e->mainloop->cached_next_time_event == e)
        e->mainloop->cached_next_time_event = NULL;
}

static void glib_time_set_destroy(pa_time_event *e, pa_time_event_destroy_cb_t cb) {
    g_assert(e);
    g_assert(!e->dead);

    e->destroy_callback = cb;
}

/* Deferred sources */

static pa_defer_event* glib_defer_new(
        pa_mainloop_api*m,
        pa_defer_event_cb_t cb,
        void *userdata) {

    pa_defer_event *e;
    pa_glib_mainloop *g;

    g_assert(m);
    g_assert(m->userdata);
    g_assert(cb);

    g = m->userdata;

    e = pa_xnew(pa_defer_event, 1);
    e->mainloop = g;
    e->dead = 0;

    e->enabled = 1;
    g->n_enabled_defer_events++;

    e->callback = cb;
    e->userdata = userdata;
    e->destroy_callback = NULL;

    PA_LLIST_PREPEND(pa_defer_event, g->defer_events, e);
    return e;
}

static void glib_defer_enable(pa_defer_event *e, int b) {
    g_assert(e);
    g_assert(!e->dead);

    if (e->enabled && !b) {
        g_assert(e->mainloop->n_enabled_defer_events > 0);
        e->mainloop->n_enabled_defer_events--;
    } else if (!e->enabled && b)
        e->mainloop->n_enabled_defer_events++;

    e->enabled = b;
}

static void glib_defer_free(pa_defer_event *e) {
    g_assert(e);
    g_assert(!e->dead);

    e->dead = 1;
    e->mainloop->defer_events_please_scan++;

    if (e->enabled) {
        g_assert(e->mainloop->n_enabled_defer_events > 0);
        e->mainloop->n_enabled_defer_events--;
    }
}

static void glib_defer_set_destroy(pa_defer_event *e, pa_defer_event_destroy_cb_t cb) {
    g_assert(e);
    g_assert(!e->dead);

    e->destroy_callback = cb;
}

/* quit() */

static void glib_quit(pa_mainloop_api*a, int retval) {

    g_warning("quit() ignored");

    /* NOOP */
}

static pa_time_event* find_next_time_event(pa_glib_mainloop *g) {
    pa_time_event *t, *n = NULL;
    g_assert(g);

    if (g->cached_next_time_event)
        return g->cached_next_time_event;

    for (t = g->time_events; t; t = t->next) {

        if (t->dead || !t->enabled)
            continue;

        if (!n || pa_timeval_cmp(&t->timeval, &n->timeval) < 0) {
            n = t;

            /* Shortcut for tv = { 0, 0 } */
            if (n->timeval.tv_sec <= 0)
                break;
        }
    }

    g->cached_next_time_event = n;
    return n;
}

static void scan_dead(pa_glib_mainloop *g) {
    g_assert(g);

    if (g->io_events_please_scan)
        cleanup_io_events(g, 0);

    if (g->time_events_please_scan)
        cleanup_time_events(g, 0);

    if (g->defer_events_please_scan)
        cleanup_defer_events(g, 0);
}

static gboolean prepare_func(GSource *source, gint *timeout) {
    pa_glib_mainloop *g = (pa_glib_mainloop*) source;

    g_assert(g);
    g_assert(timeout);

    scan_dead(g);

    if (g->n_enabled_defer_events) {
        *timeout = 0;
        return TRUE;
    } else if (g->n_enabled_time_events) {
        pa_time_event *t;
        GTimeVal now;
        struct timeval tvnow;
        pa_usec_t usec;

        t = find_next_time_event(g);
        g_assert(t);

        g_get_current_time(&now);
        tvnow.tv_sec = now.tv_sec;
        tvnow.tv_usec = now.tv_usec;

        if (pa_timeval_cmp(&t->timeval, &tvnow) <= 0) {
            *timeout = 0;
            return TRUE;
        }
        usec = pa_timeval_diff(&t->timeval, &tvnow);
        *timeout = (gint) (usec / 1000);
    } else
        *timeout = -1;

    return FALSE;
}
static gboolean check_func(GSource *source) {
    pa_glib_mainloop *g = (pa_glib_mainloop*) source;
    pa_io_event *e;

    g_assert(g);

    if (g->n_enabled_defer_events)
        return TRUE;
    else if (g->n_enabled_time_events) {
        pa_time_event *t;
        GTimeVal now;
        struct timeval tvnow;

        t = find_next_time_event(g);
        g_assert(t);

        g_get_current_time(&now);
        tvnow.tv_sec = now.tv_sec;
        tvnow.tv_usec = now.tv_usec;

        if (pa_timeval_cmp(&t->timeval, &tvnow) <= 0)
            return TRUE;
    }

    for (e = g->io_events; e; e = e->next)
        if (!e->dead && e->poll_fd.revents != 0)
            return TRUE;

    return FALSE;
}

static gboolean dispatch_func(GSource *source, GSourceFunc callback, gpointer userdata) {
    pa_glib_mainloop *g = (pa_glib_mainloop*) source;
    pa_io_event *e;

    g_assert(g);

    if (g->n_enabled_defer_events) {
        pa_defer_event *d;

        for (d = g->defer_events; d; d = d->next) {
            if (d->dead || !d->enabled)
                continue;

            break;
        }

        g_assert(d);

        d->callback(&g->api, d, d->userdata);
        return TRUE;
    }

    if (g->n_enabled_time_events) {
        GTimeVal now;
        struct timeval tvnow;
        pa_time_event *t;

        t = find_next_time_event(g);
        g_assert(t);

        g_get_current_time(&now);
        tvnow.tv_sec = now.tv_sec;
        tvnow.tv_usec = now.tv_usec;

        if (pa_timeval_cmp(&t->timeval, &tvnow) <= 0) {

            /* Disable time event */
            glib_time_restart(t, NULL);

            t->callback(&g->api, t, &t->timeval, t->userdata);
            return TRUE;
        }
    }

    for (e = g->io_events; e; e = e->next)
        if (!e->dead && e->poll_fd.revents != 0) {
            e->callback(&g->api, e, e->poll_fd.fd, map_flags_from_glib(e->poll_fd.revents), e->userdata);
            e->poll_fd.revents = 0;
            return TRUE;
        }

    return FALSE;
}

static const pa_mainloop_api vtable = {
    .userdata = NULL,

    .io_new = glib_io_new,
    .io_enable = glib_io_enable,
    .io_free = glib_io_free,
    .io_set_destroy = glib_io_set_destroy,

    .time_new = glib_time_new,
    .time_restart = glib_time_restart,
    .time_free = glib_time_free,
    .time_set_destroy = glib_time_set_destroy,

    .defer_new = glib_defer_new,
    .defer_enable = glib_defer_enable,
    .defer_free = glib_defer_free,
    .defer_set_destroy = glib_defer_set_destroy,

    .quit = glib_quit,
};

pa_glib_mainloop *pa_glib_mainloop_new(GMainContext *c) {
    pa_glib_mainloop *g;

    static GSourceFuncs source_funcs = {
        prepare_func,
        check_func,
        dispatch_func,
        NULL,
        NULL,
        NULL
    };

    g = (pa_glib_mainloop*) g_source_new(&source_funcs, sizeof(pa_glib_mainloop));
    g_main_context_ref(g->context = c ? c : g_main_context_default());

    g->api = vtable;
    g->api.userdata = g;

    PA_LLIST_HEAD_INIT(pa_io_event, g->io_events);
    PA_LLIST_HEAD_INIT(pa_time_event, g->time_events);
    PA_LLIST_HEAD_INIT(pa_defer_event, g->defer_events);

    g->n_enabled_defer_events = g->n_enabled_time_events = 0;
    g->io_events_please_scan = g->time_events_please_scan = g->defer_events_please_scan = 0;

    g->cached_next_time_event = NULL;

    g_source_attach(&g->source, g->context);
    g_source_set_can_recurse(&g->source, FALSE);

    return g;
}

void pa_glib_mainloop_free(pa_glib_mainloop* g) {
    g_assert(g);

    cleanup_io_events(g, 1);
    cleanup_defer_events(g, 1);
    cleanup_time_events(g, 1);

    g_main_context_unref(g->context);
    g_source_destroy(&g->source);
    g_source_unref(&g->source);
}

pa_mainloop_api* pa_glib_mainloop_get_api(pa_glib_mainloop *g) {
    g_assert(g);

    return &g->api;
}
