/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering
  Copyright 2006 Shams E. King

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

#include <stdarg.h>

#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/utf8.h>
#include <pulse/xmalloc.h>

#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>

#include "dbus-util.h"

struct pa_dbus_wrap_connection {
    pa_mainloop_api *mainloop;
    DBusConnection *connection;
    pa_defer_event* dispatch_event;
    bool use_rtclock:1;
};

struct timeout_data {
    pa_dbus_wrap_connection *connection;
    DBusTimeout *timeout;
};

static void dispatch_cb(pa_mainloop_api *ea, pa_defer_event *ev, void *userdata) {
    DBusConnection *conn = userdata;

    if (dbus_connection_dispatch(conn) == DBUS_DISPATCH_COMPLETE)
        /* no more data to process, disable the deferred */
        ea->defer_enable(ev, 0);
}

/* DBusDispatchStatusFunction callback for the pa mainloop */
static void dispatch_status(DBusConnection *conn, DBusDispatchStatus status, void *userdata) {
    pa_dbus_wrap_connection *c = userdata;

    pa_assert(c);

    switch(status) {

        case DBUS_DISPATCH_COMPLETE:
            c->mainloop->defer_enable(c->dispatch_event, 0);
            break;

        case DBUS_DISPATCH_DATA_REMAINS:
        case DBUS_DISPATCH_NEED_MEMORY:
        default:
            c->mainloop->defer_enable(c->dispatch_event, 1);
            break;
    }
}

static pa_io_event_flags_t get_watch_flags(DBusWatch *watch) {
    unsigned int flags;
    pa_io_event_flags_t events = 0;

    pa_assert(watch);

    flags = dbus_watch_get_flags(watch);

    /* no watch flags for disabled watches */
    if (!dbus_watch_get_enabled(watch))
        return PA_IO_EVENT_NULL;

    if (flags & DBUS_WATCH_READABLE)
        events |= PA_IO_EVENT_INPUT;
    if (flags & DBUS_WATCH_WRITABLE)
        events |= PA_IO_EVENT_OUTPUT;

    return events | PA_IO_EVENT_HANGUP | PA_IO_EVENT_ERROR;
}

/* pa_io_event_cb_t IO event handler */
static void handle_io_event(pa_mainloop_api *ea, pa_io_event *e, int fd, pa_io_event_flags_t events, void *userdata) {
    unsigned int flags = 0;
    DBusWatch *watch = userdata;

    pa_assert(fd == dbus_watch_get_unix_fd(watch));

    if (!dbus_watch_get_enabled(watch)) {
        pa_log_warn("Asked to handle disabled watch: %p %i", (void*) watch, fd);
        return;
    }

    if (events & PA_IO_EVENT_INPUT)
        flags |= DBUS_WATCH_READABLE;
    if (events & PA_IO_EVENT_OUTPUT)
        flags |= DBUS_WATCH_WRITABLE;
    if (events & PA_IO_EVENT_HANGUP)
        flags |= DBUS_WATCH_HANGUP;
    if (events & PA_IO_EVENT_ERROR)
        flags |= DBUS_WATCH_ERROR;

    dbus_watch_handle(watch, flags);
}

/* pa_time_event_cb_t timer event handler */
static void handle_time_event(pa_mainloop_api *ea, pa_time_event* e, const struct timeval *t, void *userdata) {
    struct timeval tv;
    struct timeout_data *d = userdata;

    pa_assert(d);
    pa_assert(d->connection);

    if (dbus_timeout_get_enabled(d->timeout)) {
        /* Restart it for the next scheduled time. We do this before
         * calling dbus_timeout_handle() to make sure that the time
         * event is still around. */
        ea->time_restart(e, pa_timeval_rtstore(&tv,
                                               pa_timeval_load(t) + dbus_timeout_get_interval(d->timeout) * PA_USEC_PER_MSEC,
                                               d->connection->use_rtclock));

        dbus_timeout_handle(d->timeout);
    }
}

/* DBusAddWatchFunction callback for pa mainloop */
static dbus_bool_t add_watch(DBusWatch *watch, void *data) {
    pa_dbus_wrap_connection *c = data;
    pa_io_event *ev;

    pa_assert(watch);
    pa_assert(c);

    ev = c->mainloop->io_new(
            c->mainloop,
            dbus_watch_get_unix_fd(watch),
            get_watch_flags(watch), handle_io_event, watch);

    dbus_watch_set_data(watch, ev, NULL);

    return TRUE;
}

/* DBusRemoveWatchFunction callback for pa mainloop */
static void remove_watch(DBusWatch *watch, void *data) {
    pa_dbus_wrap_connection *c = data;
    pa_io_event *ev;

    pa_assert(watch);
    pa_assert(c);

    if ((ev = dbus_watch_get_data(watch)))
        c->mainloop->io_free(ev);
}

/* DBusWatchToggledFunction callback for pa mainloop */
static void toggle_watch(DBusWatch *watch, void *data) {
    pa_dbus_wrap_connection *c = data;
    pa_io_event *ev;

    pa_assert(watch);
    pa_assert(c);

    pa_assert_se(ev = dbus_watch_get_data(watch));

    /* get_watch_flags() checks if the watch is enabled */
    c->mainloop->io_enable(ev, get_watch_flags(watch));
}

static void time_event_destroy_cb(pa_mainloop_api *a, pa_time_event *e, void *userdata) {
    pa_xfree(userdata);
}

/* DBusAddTimeoutFunction callback for pa mainloop */
static dbus_bool_t add_timeout(DBusTimeout *timeout, void *data) {
    pa_dbus_wrap_connection *c = data;
    pa_time_event *ev;
    struct timeval tv;
    struct timeout_data *d;

    pa_assert(timeout);
    pa_assert(c);

    if (!dbus_timeout_get_enabled(timeout))
        return FALSE;

    d = pa_xnew(struct timeout_data, 1);
    d->connection = c;
    d->timeout = timeout;
    ev = c->mainloop->time_new(c->mainloop, pa_timeval_rtstore(&tv, pa_rtclock_now() + dbus_timeout_get_interval(timeout) * PA_USEC_PER_MSEC, c->use_rtclock), handle_time_event, d);
    c->mainloop->time_set_destroy(ev, time_event_destroy_cb);

    dbus_timeout_set_data(timeout, ev, NULL);

    return TRUE;
}

/* DBusRemoveTimeoutFunction callback for pa mainloop */
static void remove_timeout(DBusTimeout *timeout, void *data) {
    pa_dbus_wrap_connection *c = data;
    pa_time_event *ev;

    pa_assert(timeout);
    pa_assert(c);

    if ((ev = dbus_timeout_get_data(timeout)))
        c->mainloop->time_free(ev);
}

/* DBusTimeoutToggledFunction callback for pa mainloop */
static void toggle_timeout(DBusTimeout *timeout, void *data) {
    struct timeout_data *d = data;
    pa_time_event *ev;
    struct timeval tv;

    pa_assert(d);
    pa_assert(d->connection);
    pa_assert(timeout);

    pa_assert_se(ev = dbus_timeout_get_data(timeout));

    if (dbus_timeout_get_enabled(timeout))
        d->connection->mainloop->time_restart(ev, pa_timeval_rtstore(&tv, pa_rtclock_now() + dbus_timeout_get_interval(timeout) * PA_USEC_PER_MSEC, d->connection->use_rtclock));
    else
        d->connection->mainloop->time_restart(ev, pa_timeval_rtstore(&tv, PA_USEC_INVALID, d->connection->use_rtclock));
}

static void wakeup_main(void *userdata) {
    pa_dbus_wrap_connection *c = userdata;

    pa_assert(c);

    /* this will wakeup the mainloop and dispatch events, although
     * it may not be the cleanest way of accomplishing it */
    c->mainloop->defer_enable(c->dispatch_event, 1);
}

pa_dbus_wrap_connection* pa_dbus_wrap_connection_new(pa_mainloop_api *m, bool use_rtclock, DBusBusType type, DBusError *error) {
    DBusConnection *conn;
    pa_dbus_wrap_connection *pconn;
    char *id;

    pa_assert(type == DBUS_BUS_SYSTEM || type == DBUS_BUS_SESSION || type == DBUS_BUS_STARTER);

    if (!(conn = dbus_bus_get_private(type, error)))
        return NULL;

    pconn = pa_xnew(pa_dbus_wrap_connection, 1);
    pconn->mainloop = m;
    pconn->connection = conn;
    pconn->use_rtclock = use_rtclock;

    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    dbus_connection_set_dispatch_status_function(conn, dispatch_status, pconn, NULL);
    dbus_connection_set_watch_functions(conn, add_watch, remove_watch, toggle_watch, pconn, NULL);
    dbus_connection_set_timeout_functions(conn, add_timeout, remove_timeout, toggle_timeout, pconn, NULL);
    dbus_connection_set_wakeup_main_function(conn, wakeup_main, pconn, NULL);

    pconn->dispatch_event = pconn->mainloop->defer_new(pconn->mainloop, dispatch_cb, conn);

    pa_log_debug("Successfully connected to D-Bus %s bus %s as %s",
                 type == DBUS_BUS_SYSTEM ? "system" : (type == DBUS_BUS_SESSION ? "session" : "starter"),
                 pa_strnull((id = dbus_connection_get_server_id(conn))),
                 pa_strnull(dbus_bus_get_unique_name(conn)));

    dbus_free(id);

    return pconn;
}

pa_dbus_wrap_connection* pa_dbus_wrap_connection_new_from_existing(
        pa_mainloop_api *m,
        bool use_rtclock,
        DBusConnection *conn) {
    pa_dbus_wrap_connection *pconn;

    pa_assert(m);
    pa_assert(conn);

    pconn = pa_xnew(pa_dbus_wrap_connection, 1);
    pconn->mainloop = m;
    pconn->connection = dbus_connection_ref(conn);
    pconn->use_rtclock = use_rtclock;

    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    dbus_connection_set_dispatch_status_function(conn, dispatch_status, pconn, NULL);
    dbus_connection_set_watch_functions(conn, add_watch, remove_watch, toggle_watch, pconn, NULL);
    dbus_connection_set_timeout_functions(conn, add_timeout, remove_timeout, toggle_timeout, pconn, NULL);
    dbus_connection_set_wakeup_main_function(conn, wakeup_main, pconn, NULL);

    pconn->dispatch_event = pconn->mainloop->defer_new(pconn->mainloop, dispatch_cb, conn);

    return pconn;
}

void pa_dbus_wrap_connection_free(pa_dbus_wrap_connection* c) {
    pa_assert(c);

    if (dbus_connection_get_is_connected(c->connection)) {
        dbus_connection_close(c->connection);
        /* must process remaining messages, bit of a kludge to handle
         * both unload and shutdown */
        while (dbus_connection_read_write_dispatch(c->connection, -1))
            ;
    }

    c->mainloop->defer_free(c->dispatch_event);
    dbus_connection_unref(c->connection);
    pa_xfree(c);
}

DBusConnection* pa_dbus_wrap_connection_get(pa_dbus_wrap_connection *c) {
  pa_assert(c);
  pa_assert(c->connection);

  return c->connection;
}

int pa_dbus_add_matches(DBusConnection *c, DBusError *error, ...) {
    const char *t;
    va_list ap;
    unsigned k = 0;

    pa_assert(c);
    pa_assert(error);

    va_start(ap, error);
    while ((t = va_arg(ap, const char*))) {
        dbus_bus_add_match(c, t, error);

        if (dbus_error_is_set(error))
            goto fail;

        k++;
    }
    va_end(ap);
    return 0;

fail:

    va_end(ap);
    va_start(ap, error);
    for (; k > 0; k--) {
        pa_assert_se(t = va_arg(ap, const char*));
        dbus_bus_remove_match(c, t, NULL);
    }
    va_end(ap);

    return -1;
}

void pa_dbus_remove_matches(DBusConnection *c, ...) {
    const char *t;
    va_list ap;

    pa_assert(c);

    va_start(ap, c);
    while ((t = va_arg(ap, const char*)))
        dbus_bus_remove_match(c, t, NULL);
    va_end(ap);
}

pa_dbus_pending *pa_dbus_pending_new(
        DBusConnection *c,
        DBusMessage *m,
        DBusPendingCall *pending,
        void *context_data,
        void *call_data) {

    pa_dbus_pending *p;

    pa_assert(pending);

    p = pa_xnew(pa_dbus_pending, 1);
    p->connection = c;
    p->message = m;
    p->pending = pending;
    p->context_data = context_data;
    p->call_data = call_data;

    PA_LLIST_INIT(pa_dbus_pending, p);

    return p;
}

void pa_dbus_pending_free(pa_dbus_pending *p) {
    pa_assert(p);

    if (p->pending) {
        dbus_pending_call_cancel(p->pending);
        dbus_pending_call_unref(p->pending);
    }

    if (p->message)
        dbus_message_unref(p->message);

    pa_xfree(p);
}

void pa_dbus_sync_pending_list(pa_dbus_pending **p) {
    pa_assert(p);

    while (*p && dbus_connection_read_write_dispatch((*p)->connection, -1))
        ;
}

void pa_dbus_free_pending_list(pa_dbus_pending **p) {
    pa_dbus_pending *i;

    pa_assert(p);

    while ((i = *p)) {
        PA_LLIST_REMOVE(pa_dbus_pending, *p, i);
        pa_dbus_pending_free(i);
    }
}

const char *pa_dbus_get_error_message(DBusMessage *m) {
    const char *message;

    pa_assert(m);
    pa_assert(dbus_message_get_type(m) == DBUS_MESSAGE_TYPE_ERROR);

    if (dbus_message_get_signature(m)[0] != 's')
        return "<no explanation>";

    pa_assert_se(dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &message, DBUS_TYPE_INVALID));

    return message;
}

void pa_dbus_send_error(DBusConnection *c, DBusMessage *in_reply_to, const char *name, const char *format, ...) {
    va_list ap;
    char *message;
    DBusMessage *reply = NULL;

    pa_assert(c);
    pa_assert(in_reply_to);
    pa_assert(name);
    pa_assert(format);

    va_start(ap, format);
    message = pa_vsprintf_malloc(format, ap);
    va_end(ap);
    pa_assert_se((reply = dbus_message_new_error(in_reply_to, name, message)));
    pa_assert_se(dbus_connection_send(c, reply, NULL));

    dbus_message_unref(reply);

    pa_xfree(message);
}

void pa_dbus_send_empty_reply(DBusConnection *c, DBusMessage *in_reply_to) {
    DBusMessage *reply = NULL;

    pa_assert(c);
    pa_assert(in_reply_to);

    pa_assert_se((reply = dbus_message_new_method_return(in_reply_to)));
    pa_assert_se(dbus_connection_send(c, reply, NULL));
    dbus_message_unref(reply);
}

void pa_dbus_send_basic_value_reply(DBusConnection *c, DBusMessage *in_reply_to, int type, void *data) {
    DBusMessage *reply = NULL;

    pa_assert(c);
    pa_assert(in_reply_to);
    pa_assert(dbus_type_is_basic(type));
    pa_assert(data);

    pa_assert_se((reply = dbus_message_new_method_return(in_reply_to)));
    pa_assert_se(dbus_message_append_args(reply, type, data, DBUS_TYPE_INVALID));
    pa_assert_se(dbus_connection_send(c, reply, NULL));
    dbus_message_unref(reply);
}

static const char *signature_from_basic_type(int type) {
    switch (type) {
        case DBUS_TYPE_BOOLEAN: return DBUS_TYPE_BOOLEAN_AS_STRING;
        case DBUS_TYPE_BYTE: return DBUS_TYPE_BYTE_AS_STRING;
        case DBUS_TYPE_INT16: return DBUS_TYPE_INT16_AS_STRING;
        case DBUS_TYPE_UINT16: return DBUS_TYPE_UINT16_AS_STRING;
        case DBUS_TYPE_INT32: return DBUS_TYPE_INT32_AS_STRING;
        case DBUS_TYPE_UINT32: return DBUS_TYPE_UINT32_AS_STRING;
        case DBUS_TYPE_INT64: return DBUS_TYPE_INT64_AS_STRING;
        case DBUS_TYPE_UINT64: return DBUS_TYPE_UINT64_AS_STRING;
        case DBUS_TYPE_DOUBLE: return DBUS_TYPE_DOUBLE_AS_STRING;
        case DBUS_TYPE_STRING: return DBUS_TYPE_STRING_AS_STRING;
        case DBUS_TYPE_OBJECT_PATH: return DBUS_TYPE_OBJECT_PATH_AS_STRING;
        case DBUS_TYPE_SIGNATURE: return DBUS_TYPE_SIGNATURE_AS_STRING;
        default: pa_assert_not_reached();
    }
}

void pa_dbus_send_basic_variant_reply(DBusConnection *c, DBusMessage *in_reply_to, int type, void *data) {
    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter;
    DBusMessageIter variant_iter;

    pa_assert(c);
    pa_assert(in_reply_to);
    pa_assert(dbus_type_is_basic(type));
    pa_assert(data);

    pa_assert_se((reply = dbus_message_new_method_return(in_reply_to)));
    dbus_message_iter_init_append(reply, &msg_iter);
    pa_assert_se(dbus_message_iter_open_container(&msg_iter,
                                                  DBUS_TYPE_VARIANT,
                                                  signature_from_basic_type(type),
                                                  &variant_iter));
    pa_assert_se(dbus_message_iter_append_basic(&variant_iter, type, data));
    pa_assert_se(dbus_message_iter_close_container(&msg_iter, &variant_iter));
    pa_assert_se(dbus_connection_send(c, reply, NULL));
    dbus_message_unref(reply);
}

/* Note: returns sizeof(char*) for strings, object paths and signatures. */
static unsigned basic_type_size(int type) {
    switch (type) {
        case DBUS_TYPE_BOOLEAN: return sizeof(dbus_bool_t);
        case DBUS_TYPE_BYTE: return 1;
        case DBUS_TYPE_INT16: return sizeof(dbus_int16_t);
        case DBUS_TYPE_UINT16: return sizeof(dbus_uint16_t);
        case DBUS_TYPE_INT32: return sizeof(dbus_int32_t);
        case DBUS_TYPE_UINT32: return sizeof(dbus_uint32_t);
        case DBUS_TYPE_INT64: return sizeof(dbus_int64_t);
        case DBUS_TYPE_UINT64: return sizeof(dbus_uint64_t);
        case DBUS_TYPE_DOUBLE: return sizeof(double);
        case DBUS_TYPE_STRING:
        case DBUS_TYPE_OBJECT_PATH:
        case DBUS_TYPE_SIGNATURE: return sizeof(char*);
        default: pa_assert_not_reached();
    }
}

void pa_dbus_send_basic_array_variant_reply(
        DBusConnection *c,
        DBusMessage *in_reply_to,
        int item_type,
        void *array,
        unsigned n) {
    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter;

    pa_assert(c);
    pa_assert(in_reply_to);
    pa_assert(dbus_type_is_basic(item_type));
    pa_assert(array || n == 0);

    pa_assert_se((reply = dbus_message_new_method_return(in_reply_to)));
    dbus_message_iter_init_append(reply, &msg_iter);
    pa_dbus_append_basic_array_variant(&msg_iter, item_type, array, n);
    pa_assert_se(dbus_connection_send(c, reply, NULL));
    dbus_message_unref(reply);
}

void pa_dbus_send_proplist_variant_reply(DBusConnection *c, DBusMessage *in_reply_to, pa_proplist *proplist) {
    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter;

    pa_assert(c);
    pa_assert(in_reply_to);
    pa_assert(proplist);

    pa_assert_se((reply = dbus_message_new_method_return(in_reply_to)));
    dbus_message_iter_init_append(reply, &msg_iter);
    pa_dbus_append_proplist_variant(&msg_iter, proplist);
    pa_assert_se(dbus_connection_send(c, reply, NULL));
    dbus_message_unref(reply);
}

void pa_dbus_append_basic_array(DBusMessageIter *iter, int item_type, const void *array, unsigned n) {
    DBusMessageIter array_iter;
    unsigned i;
    unsigned item_size;

    pa_assert(iter);
    pa_assert(dbus_type_is_basic(item_type));
    pa_assert(array || n == 0);

    item_size = basic_type_size(item_type);

    pa_assert_se(dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, signature_from_basic_type(item_type), &array_iter));

    for (i = 0; i < n; ++i)
        pa_assert_se(dbus_message_iter_append_basic(&array_iter, item_type, &((uint8_t*) array)[i * item_size]));

    pa_assert_se(dbus_message_iter_close_container(iter, &array_iter));
}

void pa_dbus_append_basic_variant(DBusMessageIter *iter, int type, void *data) {
    DBusMessageIter variant_iter;

    pa_assert(iter);
    pa_assert(dbus_type_is_basic(type));
    pa_assert(data);

    pa_assert_se(dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, signature_from_basic_type(type), &variant_iter));
    pa_assert_se(dbus_message_iter_append_basic(&variant_iter, type, data));
    pa_assert_se(dbus_message_iter_close_container(iter, &variant_iter));
}

void pa_dbus_append_basic_array_variant(DBusMessageIter *iter, int item_type, const void *array, unsigned n) {
    DBusMessageIter variant_iter;
    char *array_signature;

    pa_assert(iter);
    pa_assert(dbus_type_is_basic(item_type));
    pa_assert(array || n == 0);

    array_signature = pa_sprintf_malloc("a%c", *signature_from_basic_type(item_type));

    pa_assert_se(dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, array_signature, &variant_iter));
    pa_dbus_append_basic_array(&variant_iter, item_type, array, n);
    pa_assert_se(dbus_message_iter_close_container(iter, &variant_iter));

    pa_xfree(array_signature);
}

void pa_dbus_append_basic_variant_dict_entry(DBusMessageIter *dict_iter, const char *key, int type, void *data) {
    DBusMessageIter dict_entry_iter;

    pa_assert(dict_iter);
    pa_assert(key);
    pa_assert(dbus_type_is_basic(type));
    pa_assert(data);

    pa_assert_se(dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry_iter));
    pa_assert_se(dbus_message_iter_append_basic(&dict_entry_iter, DBUS_TYPE_STRING, &key));
    pa_dbus_append_basic_variant(&dict_entry_iter, type, data);
    pa_assert_se(dbus_message_iter_close_container(dict_iter, &dict_entry_iter));
}

void pa_dbus_append_basic_array_variant_dict_entry(
        DBusMessageIter *dict_iter,
        const char *key,
        int item_type,
        const void *array,
        unsigned n) {
    DBusMessageIter dict_entry_iter;

    pa_assert(dict_iter);
    pa_assert(key);
    pa_assert(dbus_type_is_basic(item_type));
    pa_assert(array || n == 0);

    pa_assert_se(dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry_iter));
    pa_assert_se(dbus_message_iter_append_basic(&dict_entry_iter, DBUS_TYPE_STRING, &key));
    pa_dbus_append_basic_array_variant(&dict_entry_iter, item_type, array, n);
    pa_assert_se(dbus_message_iter_close_container(dict_iter, &dict_entry_iter));
}

void pa_dbus_append_proplist(DBusMessageIter *iter, pa_proplist *proplist) {
    DBusMessageIter dict_iter;
    DBusMessageIter dict_entry_iter;
    DBusMessageIter array_iter;
    void *state = NULL;
    const char *key;

    pa_assert(iter);
    pa_assert(proplist);

    pa_assert_se(dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{say}", &dict_iter));

    while ((key = pa_proplist_iterate(proplist, &state))) {
        const void *value = NULL;
        size_t nbytes;

        pa_assert_se(pa_proplist_get(proplist, key, &value, &nbytes) >= 0);

        pa_assert_se(dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry_iter));

        pa_assert_se(dbus_message_iter_append_basic(&dict_entry_iter, DBUS_TYPE_STRING, &key));

        pa_assert_se(dbus_message_iter_open_container(&dict_entry_iter, DBUS_TYPE_ARRAY, "y", &array_iter));
        pa_assert_se(dbus_message_iter_append_fixed_array(&array_iter, DBUS_TYPE_BYTE, &value, nbytes));
        pa_assert_se(dbus_message_iter_close_container(&dict_entry_iter, &array_iter));

        pa_assert_se(dbus_message_iter_close_container(&dict_iter, &dict_entry_iter));
    }

    pa_assert_se(dbus_message_iter_close_container(iter, &dict_iter));
}

void pa_dbus_append_proplist_variant(DBusMessageIter *iter, pa_proplist *proplist) {
    DBusMessageIter variant_iter;

    pa_assert(iter);
    pa_assert(proplist);

    pa_assert_se(dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a{say}", &variant_iter));
    pa_dbus_append_proplist(&variant_iter, proplist);
    pa_assert_se(dbus_message_iter_close_container(iter, &variant_iter));
}

void pa_dbus_append_proplist_variant_dict_entry(DBusMessageIter *dict_iter, const char *key, pa_proplist *proplist) {
    DBusMessageIter dict_entry_iter;

    pa_assert(dict_iter);
    pa_assert(key);
    pa_assert(proplist);

    pa_assert_se(dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry_iter));
    pa_assert_se(dbus_message_iter_append_basic(&dict_entry_iter, DBUS_TYPE_STRING, &key));
    pa_dbus_append_proplist_variant(&dict_entry_iter, proplist);
    pa_assert_se(dbus_message_iter_close_container(dict_iter, &dict_entry_iter));
}

pa_proplist *pa_dbus_get_proplist_arg(DBusConnection *c, DBusMessage *msg, DBusMessageIter *iter) {
    DBusMessageIter dict_iter;
    DBusMessageIter dict_entry_iter;
    char *signature;
    pa_proplist *proplist = NULL;
    const char *key = NULL;
    const uint8_t *value = NULL;
    int value_length = 0;

    pa_assert(c);
    pa_assert(msg);
    pa_assert(iter);

    pa_assert(signature = dbus_message_iter_get_signature(iter));
    pa_assert_se(pa_streq(signature, "a{say}"));

    dbus_free(signature);

    proplist = pa_proplist_new();

    dbus_message_iter_recurse(iter, &dict_iter);

    while (dbus_message_iter_get_arg_type(&dict_iter) != DBUS_TYPE_INVALID) {
        dbus_message_iter_recurse(&dict_iter, &dict_entry_iter);

        dbus_message_iter_get_basic(&dict_entry_iter, &key);
        dbus_message_iter_next(&dict_entry_iter);

        if (strlen(key) <= 0 || !pa_ascii_valid(key)) {
            pa_dbus_send_error(c, msg, DBUS_ERROR_INVALID_ARGS, "Invalid property list key: '%s'.", key);
            goto fail;
        }

        dbus_message_iter_get_fixed_array(&dict_entry_iter, &value, &value_length);

        pa_assert(value_length >= 0);

        pa_assert_se(pa_proplist_set(proplist, key, value, value_length) >= 0);

        dbus_message_iter_next(&dict_iter);
    }

    dbus_message_iter_next(iter);

    return proplist;

fail:
    if (proplist)
        pa_proplist_free(proplist);

    return NULL;
}
