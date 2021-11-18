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

#include <stdio.h>

#include <pulse/xmalloc.h>

#include <pulsecore/llist.h>
#include <pulsecore/log.h>
#include <pulsecore/shared.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

#include "x11wrap.h"

typedef struct pa_x11_internal pa_x11_internal;

struct pa_x11_internal {
    PA_LLIST_FIELDS(pa_x11_internal);
    pa_x11_wrapper *wrapper;
    pa_io_event* io_event;
    int fd;
};

struct pa_x11_wrapper {
    PA_REFCNT_DECLARE;
    pa_core *core;

    char *property_name;
    Display *display;

    pa_defer_event* defer_event;
    pa_io_event* io_event;

    PA_LLIST_HEAD(pa_x11_client, clients);
    PA_LLIST_HEAD(pa_x11_internal, internals);
};

struct pa_x11_client {
    PA_LLIST_FIELDS(pa_x11_client);
    pa_x11_wrapper *wrapper;
    pa_x11_event_cb_t event_cb;
    pa_x11_kill_cb_t kill_cb;
    void *userdata;
};

/* Dispatch all pending X11 events */
static void work(pa_x11_wrapper *w) {
    pa_assert(w);
    pa_assert(PA_REFCNT_VALUE(w) >= 1);

    pa_x11_wrapper_ref(w);

    while (XPending(w->display)) {
        pa_x11_client *c, *n;
        XEvent e;
        XNextEvent(w->display, &e);

        for (c = w->clients; c; c = n) {
            n = c->next;

            if (c->event_cb)
                if (c->event_cb(w, &e, c->userdata) != 0)
                    break;
        }
    }

    pa_x11_wrapper_unref(w);
}

/* IO notification event for the X11 display connection */
static void display_io_event(pa_mainloop_api *m, pa_io_event *e, int fd, pa_io_event_flags_t f, void *userdata) {
    pa_x11_wrapper *w = userdata;

    pa_assert(m);
    pa_assert(e);
    pa_assert(fd >= 0);
    pa_assert(w);
    pa_assert(PA_REFCNT_VALUE(w) >= 1);

    work(w);
}

/* Deferred notification event. Called once each main loop iteration */
static void defer_event(pa_mainloop_api *m, pa_defer_event *e, void *userdata) {
    pa_x11_wrapper *w = userdata;

    pa_assert(m);
    pa_assert(e);
    pa_assert(w);
    pa_assert(PA_REFCNT_VALUE(w) >= 1);

    m->defer_enable(e, 0);

    work(w);
}

/* IO notification event for X11 internal connections */
static void internal_io_event(pa_mainloop_api *m, pa_io_event *e, int fd, pa_io_event_flags_t f, void *userdata) {
    pa_x11_wrapper *w = userdata;

    pa_assert(m);
    pa_assert(e);
    pa_assert(fd >= 0);
    pa_assert(w);
    pa_assert(PA_REFCNT_VALUE(w) >= 1);

    XProcessInternalConnection(w->display, fd);

    work(w);
}

/* Add a new IO source for the specified X11 internal connection */
static pa_x11_internal* x11_internal_add(pa_x11_wrapper *w, int fd) {
    pa_x11_internal *i;
    pa_assert(fd >= 0);

    i = pa_xnew(pa_x11_internal, 1);
    i->wrapper = w;
    i->io_event = w->core->mainloop->io_new(w->core->mainloop, fd, PA_IO_EVENT_INPUT, internal_io_event, w);
    i->fd = fd;

    PA_LLIST_PREPEND(pa_x11_internal, w->internals, i);
    return i;
}

/* Remove an IO source for an X11 internal connection */
static void x11_internal_remove(pa_x11_wrapper *w, pa_x11_internal *i) {
    pa_assert(i);

    PA_LLIST_REMOVE(pa_x11_internal, w->internals, i);
    w->core->mainloop->io_free(i->io_event);
    pa_xfree(i);
}

/* Implementation of XConnectionWatchProc */
static void x11_watch(Display *display, XPointer userdata, int fd, Bool opening, XPointer *watch_data) {
    pa_x11_wrapper *w = (pa_x11_wrapper*) userdata;

    pa_assert(display);
    pa_assert(w);
    pa_assert(fd >= 0);

    if (opening)
        *watch_data = (XPointer) x11_internal_add(w, fd);
    else
        x11_internal_remove(w, (pa_x11_internal*) *watch_data);
}

static pa_x11_wrapper* x11_wrapper_new(pa_core *c, const char *name, const char *t) {
    pa_x11_wrapper*w;
    Display *d;

    if (!(d = XOpenDisplay(name))) {
        pa_log("XOpenDisplay() failed");
        return NULL;
    }

    w = pa_xnew(pa_x11_wrapper, 1);
    PA_REFCNT_INIT(w);
    w->core = c;
    w->property_name = pa_xstrdup(t);
    w->display = d;

    PA_LLIST_HEAD_INIT(pa_x11_client, w->clients);
    PA_LLIST_HEAD_INIT(pa_x11_internal, w->internals);

    w->defer_event = c->mainloop->defer_new(c->mainloop, defer_event, w);
    w->io_event = c->mainloop->io_new(c->mainloop, ConnectionNumber(d), PA_IO_EVENT_INPUT, display_io_event, w);

    XAddConnectionWatch(d, x11_watch, (XPointer) w);

    pa_assert_se(pa_shared_set(c, w->property_name, w) >= 0);

    return w;
}

static void x11_wrapper_free(pa_x11_wrapper*w) {
    pa_assert(w);

    pa_assert_se(pa_shared_remove(w->core, w->property_name) >= 0);

    pa_assert(!w->clients);

    XRemoveConnectionWatch(w->display, x11_watch, (XPointer) w);
    XCloseDisplay(w->display);

    w->core->mainloop->io_free(w->io_event);
    w->core->mainloop->defer_free(w->defer_event);

    while (w->internals)
        x11_internal_remove(w, w->internals);

    pa_xfree(w->property_name);
    pa_xfree(w);
}

pa_x11_wrapper* pa_x11_wrapper_get(pa_core *c, const char *name) {
    char t[256];
    pa_x11_wrapper *w;

    pa_core_assert_ref(c);

    pa_snprintf(t, sizeof(t), "x11-wrapper%s%s", name ? "@" : "", name ? name : "");

    if ((w = pa_shared_get(c, t)))
        return pa_x11_wrapper_ref(w);

    return x11_wrapper_new(c, name, t);
}

pa_x11_wrapper* pa_x11_wrapper_ref(pa_x11_wrapper *w) {
    pa_assert(w);
    pa_assert(PA_REFCNT_VALUE(w) >= 1);

    PA_REFCNT_INC(w);
    return w;
}

void pa_x11_wrapper_unref(pa_x11_wrapper* w) {
    pa_assert(w);
    pa_assert(PA_REFCNT_VALUE(w) >= 1);

    if (PA_REFCNT_DEC(w) > 0)
        return;

    x11_wrapper_free(w);
}

Display *pa_x11_wrapper_get_display(pa_x11_wrapper *w) {
    pa_assert(w);
    pa_assert(PA_REFCNT_VALUE(w) >= 1);

    /* Somebody is using us, schedule a output buffer flush */
    w->core->mainloop->defer_enable(w->defer_event, 1);

    return w->display;
}

xcb_connection_t *pa_x11_wrapper_get_xcb_connection(pa_x11_wrapper *w) {
    return XGetXCBConnection(pa_x11_wrapper_get_display(w));
}

void pa_x11_wrapper_kill(pa_x11_wrapper *w) {
    pa_x11_client *c, *n;

    pa_assert(w);

    pa_x11_wrapper_ref(w);

    for (c = w->clients; c; c = n) {
        n = c->next;

        if (c->kill_cb)
            c->kill_cb(w, c->userdata);
    }

    pa_x11_wrapper_unref(w);
}

pa_x11_client* pa_x11_client_new(pa_x11_wrapper *w, pa_x11_event_cb_t event_cb, pa_x11_kill_cb_t kill_cb, void *userdata) {
    pa_x11_client *c;

    pa_assert(w);
    pa_assert(PA_REFCNT_VALUE(w) >= 1);

    c = pa_xnew(pa_x11_client, 1);
    c->wrapper = w;
    c->event_cb = event_cb;
    c->kill_cb = kill_cb;
    c->userdata = userdata;

    PA_LLIST_PREPEND(pa_x11_client, w->clients, c);

    return c;
}

void pa_x11_client_free(pa_x11_client *c) {
    pa_assert(c);
    pa_assert(c->wrapper);
    pa_assert(PA_REFCNT_VALUE(c->wrapper) >= 1);

    PA_LLIST_REMOVE(pa_x11_client, c->wrapper->clients, c);
    pa_xfree(c);
}
