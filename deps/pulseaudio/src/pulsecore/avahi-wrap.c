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
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>

#include "avahi-wrap.h"

typedef struct {
    AvahiPoll api;
    pa_mainloop_api *mainloop;
} pa_avahi_poll;

struct AvahiWatch {
    pa_io_event *io_event;
    pa_avahi_poll *avahi_poll;
    AvahiWatchEvent current_event;
    AvahiWatchCallback callback;
    void *userdata;
};

static AvahiWatchEvent translate_io_flags_back(pa_io_event_flags_t e) {
    return
        (e & PA_IO_EVENT_INPUT ? AVAHI_WATCH_IN : 0) |
        (e & PA_IO_EVENT_OUTPUT ? AVAHI_WATCH_OUT : 0) |
        (e & PA_IO_EVENT_ERROR ? AVAHI_WATCH_ERR : 0) |
        (e & PA_IO_EVENT_HANGUP ? AVAHI_WATCH_HUP : 0);
}

static pa_io_event_flags_t translate_io_flags(AvahiWatchEvent e) {
    return
        (e & AVAHI_WATCH_IN ? PA_IO_EVENT_INPUT : 0) |
        (e & AVAHI_WATCH_OUT ? PA_IO_EVENT_OUTPUT : 0) |
        (e & AVAHI_WATCH_ERR ? PA_IO_EVENT_ERROR : 0) |
        (e & AVAHI_WATCH_HUP ? PA_IO_EVENT_HANGUP : 0);
}

static void watch_callback(pa_mainloop_api*a, pa_io_event* e, int fd, pa_io_event_flags_t events, void *userdata) {
    AvahiWatch *w = userdata;

    pa_assert(a);
    pa_assert(e);
    pa_assert(w);

    w->current_event = translate_io_flags_back(events);
    w->callback(w, fd, w->current_event, w->userdata);
    w->current_event = 0;
}

static AvahiWatch* watch_new(const AvahiPoll *api, int fd, AvahiWatchEvent event, AvahiWatchCallback callback, void *userdata) {
    pa_avahi_poll *p;
    AvahiWatch *w;

    pa_assert(api);
    pa_assert(fd >= 0);
    pa_assert(callback);
    pa_assert_se(p = api->userdata);

    w = pa_xnew(AvahiWatch, 1);
    w->avahi_poll = p;
    w->current_event = 0;
    w->callback = callback;
    w->userdata = userdata;
    w->io_event = p->mainloop->io_new(p->mainloop, fd, translate_io_flags(event), watch_callback, w);

    return w;
}

static void watch_update(AvahiWatch *w, AvahiWatchEvent event) {
    pa_assert(w);

    w->avahi_poll->mainloop->io_enable(w->io_event, translate_io_flags(event));
}

static AvahiWatchEvent watch_get_events(AvahiWatch *w) {
    pa_assert(w);

    return w->current_event;
}

static void watch_free(AvahiWatch *w) {
    pa_assert(w);

    w->avahi_poll->mainloop->io_free(w->io_event);
    pa_xfree(w);
}

struct AvahiTimeout {
    pa_time_event *time_event;
    pa_avahi_poll *avahi_poll;
    AvahiTimeoutCallback callback;
    void *userdata;
};

static void timeout_callback(pa_mainloop_api*a, pa_time_event* e, const struct timeval *t, void *userdata) {
    AvahiTimeout *to = userdata;

    pa_assert(a);
    pa_assert(e);

    to->callback(to, to->userdata);
}

static AvahiTimeout* timeout_new(const AvahiPoll *api, const struct timeval *tv, AvahiTimeoutCallback callback, void *userdata) {
    pa_avahi_poll *p;
    AvahiTimeout *t;

    pa_assert(api);
    pa_assert(callback);
    pa_assert_se(p = api->userdata);

    t = pa_xnew(AvahiTimeout, 1);
    t->avahi_poll = p;
    t->callback = callback;
    t->userdata = userdata;

    t->time_event = tv ? p->mainloop->time_new(p->mainloop, tv, timeout_callback, t) : NULL;

    return t;
}

static void timeout_update(AvahiTimeout *t, const struct timeval *tv) {

    pa_assert(t);

    if (t->time_event && tv)
        t->avahi_poll->mainloop->time_restart(t->time_event, tv);
    else if (!t->time_event && tv)
        t->time_event = t->avahi_poll->mainloop->time_new(t->avahi_poll->mainloop, tv, timeout_callback, t);
    else if (t->time_event && !tv) {
        t->avahi_poll->mainloop->time_free(t->time_event);
        t->time_event = NULL;
    }
}

static void timeout_free(AvahiTimeout *t) {
    pa_assert(t);

    if (t->time_event)
        t->avahi_poll->mainloop->time_free(t->time_event);
    pa_xfree(t);
}

AvahiPoll* pa_avahi_poll_new(pa_mainloop_api *m) {
    pa_avahi_poll *p;

    pa_assert(m);

    p = pa_xnew(pa_avahi_poll, 1);

    p->api.userdata = p;
    p->api.watch_new = watch_new;
    p->api.watch_update = watch_update;
    p->api.watch_get_events = watch_get_events;
    p->api.watch_free = watch_free;
    p->api.timeout_new = timeout_new;
    p->api.timeout_update = timeout_update;
    p->api.timeout_free = timeout_free;
    p->mainloop = m;

    return &p->api;
}

void pa_avahi_poll_free(AvahiPoll *api) {
    pa_avahi_poll *p;
    pa_assert(api);
    pa_assert_se(p = api->userdata);

    pa_xfree(p);
}

