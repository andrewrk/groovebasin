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

#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "core-subscribe.h"

/* The subscription subsystem may be used to be notified whenever an
 * entity (sink, source, ...) is created or deleted. Modules may
 * register a callback function that is called whenever an event
 * matching a subscription mask happens. The execution of the callback
 * function is postponed to the next main loop iteration, i.e. is not
 * called from within the stack frame the entity was created in. */

struct pa_subscription {
    pa_core *core;
    bool dead;

    pa_subscription_cb_t callback;
    void *userdata;
    pa_subscription_mask_t mask;

    PA_LLIST_FIELDS(pa_subscription);
};

struct pa_subscription_event {
    pa_core *core;

    pa_subscription_event_type_t type;
    uint32_t index;

    PA_LLIST_FIELDS(pa_subscription_event);
};

static void sched_event(pa_core *c);

/* Allocate a new subscription object for the given subscription mask. Use the specified callback function and user data */
pa_subscription* pa_subscription_new(pa_core *c, pa_subscription_mask_t m, pa_subscription_cb_t callback, void *userdata) {
    pa_subscription *s;

    pa_assert(c);
    pa_assert(m);
    pa_assert(callback);

    s = pa_xnew(pa_subscription, 1);
    s->core = c;
    s->dead = false;
    s->callback = callback;
    s->userdata = userdata;
    s->mask = m;

    PA_LLIST_PREPEND(pa_subscription, c->subscriptions, s);
    return s;
}

/* Free a subscription object, effectively marking it for deletion */
void pa_subscription_free(pa_subscription*s) {
    pa_assert(s);
    pa_assert(!s->dead);

    s->dead = true;
    sched_event(s->core);
}

static void free_subscription(pa_subscription *s) {
    pa_assert(s);
    pa_assert(s->core);

    PA_LLIST_REMOVE(pa_subscription, s->core->subscriptions, s);
    pa_xfree(s);
}

static void free_event(pa_subscription_event *s) {
    pa_assert(s);
    pa_assert(s->core);

    if (!s->next)
        s->core->subscription_event_last = s->prev;

    PA_LLIST_REMOVE(pa_subscription_event, s->core->subscription_event_queue, s);
    pa_xfree(s);
}

/* Free all subscription objects */
void pa_subscription_free_all(pa_core *c) {
    pa_assert(c);

    while (c->subscriptions)
        free_subscription(c->subscriptions);

    while (c->subscription_event_queue)
        free_event(c->subscription_event_queue);

    if (c->subscription_defer_event) {
        c->mainloop->defer_free(c->subscription_defer_event);
        c->subscription_defer_event = NULL;
    }
}

#ifdef DEBUG
static void dump_event(const char * prefix, pa_subscription_event*e) {
    const char * const fac_table[] = {
        [PA_SUBSCRIPTION_EVENT_SINK] = "SINK",
        [PA_SUBSCRIPTION_EVENT_SOURCE] = "SOURCE",
        [PA_SUBSCRIPTION_EVENT_SINK_INPUT] = "SINK_INPUT",
        [PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT] = "SOURCE_OUTPUT",
        [PA_SUBSCRIPTION_EVENT_MODULE] = "MODULE",
        [PA_SUBSCRIPTION_EVENT_CLIENT] = "CLIENT",
        [PA_SUBSCRIPTION_EVENT_SAMPLE_CACHE] = "SAMPLE_CACHE",
        [PA_SUBSCRIPTION_EVENT_SERVER] = "SERVER",
        [PA_SUBSCRIPTION_EVENT_AUTOLOAD] = "AUTOLOAD",
        [PA_SUBSCRIPTION_EVENT_CARD] = "CARD"
    };

    const char * const type_table[] = {
        [PA_SUBSCRIPTION_EVENT_NEW] = "NEW",
        [PA_SUBSCRIPTION_EVENT_CHANGE] = "CHANGE",
        [PA_SUBSCRIPTION_EVENT_REMOVE] = "REMOVE"
    };

    pa_log_debug("%s event (%s|%s|%u)",
           prefix,
           fac_table[e->type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK],
           type_table[e->type & PA_SUBSCRIPTION_EVENT_TYPE_MASK],
           e->index);
}
#endif

/* Deferred callback for dispatching subscription events */
static void defer_cb(pa_mainloop_api *m, pa_defer_event *de, void *userdata) {
    pa_core *c = userdata;
    pa_subscription *s;

    pa_assert(c->mainloop == m);
    pa_assert(c);
    pa_assert(c->subscription_defer_event == de);

    c->mainloop->defer_enable(c->subscription_defer_event, 0);

    /* Dispatch queued events */

    while (c->subscription_event_queue) {
        pa_subscription_event *e = c->subscription_event_queue;

        for (s = c->subscriptions; s; s = s->next) {

            if (!s->dead && pa_subscription_match_flags(s->mask, e->type))
                s->callback(c, e->type, e->index, s->userdata);
        }

#ifdef DEBUG
        dump_event("Dispatched", e);
#endif
        free_event(e);
    }

    /* Remove dead subscriptions */

    s = c->subscriptions;
    while (s) {
        pa_subscription *n = s->next;
        if (s->dead)
            free_subscription(s);
        s = n;
    }
}

/* Schedule an mainloop event so that a pending subscription event is dispatched */
static void sched_event(pa_core *c) {
    pa_assert(c);

    if (!c->subscription_defer_event) {
        c->subscription_defer_event = c->mainloop->defer_new(c->mainloop, defer_cb, c);
        pa_assert(c->subscription_defer_event);
    }

    c->mainloop->defer_enable(c->subscription_defer_event, 1);
}

/* Append a new subscription event to the subscription event queue and schedule a main loop event */
void pa_subscription_post(pa_core *c, pa_subscription_event_type_t t, uint32_t idx) {
    pa_subscription_event *e;
    pa_assert(c);

    /* No need for queuing subscriptions of no one is listening */
    if (!c->subscriptions)
        return;

    if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) != PA_SUBSCRIPTION_EVENT_NEW) {
        pa_subscription_event *i, *n;

        /* Check for duplicates */
        for (i = c->subscription_event_last; i; i = n) {
            n = i->prev;

            /* not the same object type */
            if (((t ^ i->type) & PA_SUBSCRIPTION_EVENT_FACILITY_MASK))
                continue;

            /* not the same object */
            if (i->index != idx)
                continue;

            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
                /* This object is being removed, hence there is no
                 * point in keeping the old events regarding this
                 * entry in the queue. */

                free_event(i);
                pa_log_debug("Dropped redundant event due to remove event.");
                continue;
            }

            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_CHANGE) {
                /* This object has changed. If a "new" or "change" event for
                 * this object is still in the queue we can exit. */

                pa_log_debug("Dropped redundant event due to change event.");
                return;
            }
        }
    }

    e = pa_xnew(pa_subscription_event, 1);
    e->core = c;
    e->type = t;
    e->index = idx;

    PA_LLIST_INSERT_AFTER(pa_subscription_event, c->subscription_event_queue, c->subscription_event_last, e);
    c->subscription_event_last = e;

#ifdef DEBUG
    dump_event("Queued", e);
#endif

    sched_event(c);
}
