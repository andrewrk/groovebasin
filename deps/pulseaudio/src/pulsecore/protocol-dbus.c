/***
  This file is part of PulseAudio.

  Copyright 2009 Tanu Kaskinen

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

#include <dbus/dbus.h>

#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/idxset.h>
#include <pulsecore/shared.h>
#include <pulsecore/strbuf.h>

#include "protocol-dbus.h"

struct pa_dbus_protocol {
    PA_REFCNT_DECLARE;

    pa_core *core;
    pa_hashmap *objects; /* Object path -> struct object_entry */
    pa_hashmap *connections; /* DBusConnection -> struct connection_entry */
    pa_idxset *extensions; /* Strings */

    pa_hook hooks[PA_DBUS_PROTOCOL_HOOK_MAX];
};

struct object_entry {
    char *path;
    pa_hashmap *interfaces; /* Interface name -> struct interface_entry */
    char *introspection;
};

struct connection_entry {
    DBusConnection *connection;
    pa_client *client;

    bool listening_for_all_signals;

    /* Contains object paths. If this is empty, then signals from all objects
     * are accepted. Only used when listening_for_all_signals == true. */
    pa_idxset *all_signals_objects;

    /* Signal name -> signal paths entry. The entries contain object paths. If
     * a path set is empty, then that signal is accepted from all objects. This
     * variable is only used when listening_for_all_signals == false. */
    pa_hashmap *listening_signals;
};

/* Only used in connection entries' listening_signals hashmap. */
struct signal_paths_entry {
    char *signal;
    pa_idxset *paths;
};

struct interface_entry {
    char *name;
    pa_hashmap *method_handlers;
    pa_hashmap *method_signatures; /* Derived from method_handlers. Contains only "in" arguments. */
    pa_hashmap *property_handlers;
    pa_dbus_receive_cb_t get_all_properties_cb;
    pa_dbus_signal_info *signals;
    unsigned n_signals;
    void *userdata;
};

char *pa_get_dbus_address_from_server_type(pa_server_type_t server_type) {
    char *address = NULL;
    char *runtime_path = NULL;
    char *escaped_path = NULL;

    switch (server_type) {
        case PA_SERVER_TYPE_USER:
            pa_assert_se((runtime_path = pa_runtime_path(PA_DBUS_SOCKET_NAME)));
            pa_assert_se((escaped_path = dbus_address_escape_value(runtime_path)));
            address = pa_sprintf_malloc("unix:path=%s", escaped_path);
            break;

        case PA_SERVER_TYPE_SYSTEM:
            pa_assert_se((escaped_path = dbus_address_escape_value(PA_DBUS_SYSTEM_SOCKET_PATH)));
            address = pa_sprintf_malloc("unix:path=%s", escaped_path);
            break;

        case PA_SERVER_TYPE_NONE:
            address = pa_xnew0(char, 1);
            break;

        default:
            pa_assert_not_reached();
    }

    pa_xfree(runtime_path);
    dbus_free(escaped_path);

    return address;
}

static pa_dbus_protocol *dbus_protocol_new(pa_core *c) {
    pa_dbus_protocol *p;
    unsigned i;

    pa_assert(c);

    p = pa_xnew(pa_dbus_protocol, 1);
    PA_REFCNT_INIT(p);
    p->core = c;
    p->objects = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    p->connections = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    p->extensions = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    for (i = 0; i < PA_DBUS_PROTOCOL_HOOK_MAX; ++i)
        pa_hook_init(&p->hooks[i], p);

    pa_assert_se(pa_shared_set(c, "dbus-protocol", p) >= 0);

    return p;
}

pa_dbus_protocol* pa_dbus_protocol_get(pa_core *c) {
    pa_dbus_protocol *p;

    if ((p = pa_shared_get(c, "dbus-protocol")))
        return pa_dbus_protocol_ref(p);

    return dbus_protocol_new(c);
}

pa_dbus_protocol* pa_dbus_protocol_ref(pa_dbus_protocol *p) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    PA_REFCNT_INC(p);

    return p;
}

void pa_dbus_protocol_unref(pa_dbus_protocol *p) {
    unsigned i;

    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    if (PA_REFCNT_DEC(p) > 0)
        return;

    pa_assert(pa_hashmap_isempty(p->objects));
    pa_assert(pa_hashmap_isempty(p->connections));
    pa_assert(pa_idxset_isempty(p->extensions));

    pa_hashmap_free(p->objects);
    pa_hashmap_free(p->connections);
    pa_idxset_free(p->extensions, NULL);

    for (i = 0; i < PA_DBUS_PROTOCOL_HOOK_MAX; ++i)
        pa_hook_done(&p->hooks[i]);

    pa_assert_se(pa_shared_remove(p->core, "dbus-protocol") >= 0);

    pa_xfree(p);
}

static void update_introspection(struct object_entry *oe) {
    pa_strbuf *buf;
    void *interfaces_state = NULL;
    struct interface_entry *iface_entry = NULL;

    pa_assert(oe);

    buf = pa_strbuf_new();
    pa_strbuf_puts(buf, DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
    pa_strbuf_puts(buf, "<node>\n");

    PA_HASHMAP_FOREACH(iface_entry, oe->interfaces, interfaces_state) {
        pa_dbus_method_handler *method_handler;
        pa_dbus_property_handler *property_handler;
        void *handlers_state = NULL;
        unsigned i;
        unsigned j;

        pa_strbuf_printf(buf, " <interface name=\"%s\">\n", iface_entry->name);

        PA_HASHMAP_FOREACH(method_handler, iface_entry->method_handlers, handlers_state) {
            pa_strbuf_printf(buf, "  <method name=\"%s\">\n", method_handler->method_name);

            for (i = 0; i < method_handler->n_arguments; ++i)
                pa_strbuf_printf(buf, "   <arg name=\"%s\" type=\"%s\" direction=\"%s\"/>\n",
                                 method_handler->arguments[i].name,
                                 method_handler->arguments[i].type,
                                 method_handler->arguments[i].direction);

            pa_strbuf_puts(buf, "  </method>\n");
        }

        handlers_state = NULL;

        PA_HASHMAP_FOREACH(property_handler, iface_entry->property_handlers, handlers_state)
            pa_strbuf_printf(buf, "  <property name=\"%s\" type=\"%s\" access=\"%s\"/>\n",
                             property_handler->property_name,
                             property_handler->type,
                             property_handler->get_cb ? (property_handler->set_cb ? "readwrite" : "read") : "write");

        for (i = 0; i < iface_entry->n_signals; ++i) {
            pa_strbuf_printf(buf, "  <signal name=\"%s\">\n", iface_entry->signals[i].name);

            for (j = 0; j < iface_entry->signals[i].n_arguments; ++j)
                pa_strbuf_printf(buf, "   <arg name=\"%s\" type=\"%s\"/>\n", iface_entry->signals[i].arguments[j].name,
                                                                             iface_entry->signals[i].arguments[j].type);

            pa_strbuf_puts(buf, "  </signal>\n");
        }

        pa_strbuf_puts(buf, " </interface>\n");
    }

    pa_strbuf_puts(buf, " <interface name=\"" DBUS_INTERFACE_INTROSPECTABLE "\">\n"
                        "  <method name=\"Introspect\">\n"
                        "   <arg name=\"data\" type=\"s\" direction=\"out\"/>\n"
                        "  </method>\n"
                        " </interface>\n"
                        " <interface name=\"" DBUS_INTERFACE_PROPERTIES "\">\n"
                        "  <method name=\"Get\">\n"
                        "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
                        "   <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"
                        "   <arg name=\"value\" type=\"v\" direction=\"out\"/>\n"
                        "  </method>\n"
                        "  <method name=\"Set\">\n"
                        "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
                        "   <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"
                        "   <arg name=\"value\" type=\"v\" direction=\"in\"/>\n"
                        "  </method>\n"
                        "  <method name=\"GetAll\">\n"
                        "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
                        "   <arg name=\"props\" type=\"a{sv}\" direction=\"out\"/>\n"
                        "  </method>\n"
                        " </interface>\n");

    pa_strbuf_puts(buf, "</node>\n");

    pa_xfree(oe->introspection);
    oe->introspection = pa_strbuf_to_string_free(buf);
}

/* Return value of find_handler() and its subfunctions. */
enum find_result_t {
    /* The received message is a valid .Get call. */
    FOUND_GET_PROPERTY,

    /* The received message is a valid .Set call. */
    FOUND_SET_PROPERTY,

    /* The received message is a valid .GetAll call. */
    FOUND_GET_ALL,

    /* The received message is a valid method call. */
    FOUND_METHOD,

    /* The interface of the received message hasn't been registered for the
     * destination object. */
    NO_SUCH_INTERFACE,

    /* No property handler was found for the received .Get or .Set call. */
    NO_SUCH_PROPERTY,

    /* The interface argument of a property call didn't match any registered
     * interface. */
    NO_SUCH_PROPERTY_INTERFACE,

    /* The received message called .Get or .Set for a property whose access
     * mode doesn't match the call. */
    PROPERTY_ACCESS_DENIED,

    /* The new value signature of a .Set call didn't match the expected
     * signature. */
    INVALID_PROPERTY_SIG,

    /* No method handler was found for the received message. */
    NO_SUCH_METHOD,

    /* The signature of the received message didn't match the expected
     * signature. Despite the name, this can also be returned for a property
     * call if its message signature is invalid. */
    INVALID_METHOD_SIG
};

/* Data for resolving the correct reaction to a received message. */
struct call_info {
    DBusMessage *message; /* The received message. */
    struct object_entry *obj_entry;
    const char *interface; /* Destination interface name (extracted from the message). */
    struct interface_entry *iface_entry;

    const char *property; /* Property name (extracted from the message). */
    const char *property_interface; /* The interface argument of a property call is stored here. */
    pa_dbus_property_handler *property_handler;
    const char *expected_property_sig; /* Property signature from the introspection data. */
    char *property_sig; /* The signature of the new value in the received .Set message. */
    DBusMessageIter variant_iter; /* Iterator pointing to the beginning of the new value variant of a .Set call. */

    const char *method; /* Method name (extracted from the message). */
    pa_dbus_method_handler *method_handler;
    const char *expected_method_sig; /* Method signature from the introspection data. */
    const char *method_sig; /* The signature of the received message. */
};

/* Called when call_info->property has been set and the property interface has
 * not been given. In case of a Set call, call_info->property_sig is also set,
 * which is checked against the expected value in this function. */
static enum find_result_t find_handler_by_property(struct call_info *call_info) {
    void *state = NULL;

    pa_assert(call_info);

    PA_HASHMAP_FOREACH(call_info->iface_entry, call_info->obj_entry->interfaces, state) {
        if ((call_info->property_handler = pa_hashmap_get(call_info->iface_entry->property_handlers, call_info->property))) {
            if (pa_streq(call_info->method, "Get"))
                return call_info->property_handler->get_cb ? FOUND_GET_PROPERTY : PROPERTY_ACCESS_DENIED;

            else if (pa_streq(call_info->method, "Set")) {
                call_info->expected_property_sig = call_info->property_handler->type;

                if (pa_streq(call_info->property_sig, call_info->expected_property_sig))
                    return call_info->property_handler->set_cb ? FOUND_SET_PROPERTY : PROPERTY_ACCESS_DENIED;
                else
                    return INVALID_PROPERTY_SIG;

            } else
                pa_assert_not_reached();
        }
    }

    return NO_SUCH_PROPERTY;
}

static enum find_result_t find_handler_by_method(struct call_info *call_info) {
    void *state = NULL;

    pa_assert(call_info);

    PA_HASHMAP_FOREACH(call_info->iface_entry, call_info->obj_entry->interfaces, state) {
        if ((call_info->method_handler = pa_hashmap_get(call_info->iface_entry->method_handlers, call_info->method))) {
            pa_assert_se(call_info->expected_method_sig = pa_hashmap_get(call_info->iface_entry->method_signatures, call_info->method));

            if (pa_streq(call_info->method_sig, call_info->expected_method_sig))
                return FOUND_METHOD;
            else
                return INVALID_METHOD_SIG;
        }
    }

    return NO_SUCH_METHOD;
}

static enum find_result_t find_handler_from_properties_call(struct call_info *call_info) {
    pa_assert(call_info);

    if (pa_streq(call_info->method, "GetAll")) {
        call_info->expected_method_sig = "s";
        if (!pa_streq(call_info->method_sig, call_info->expected_method_sig))
            return INVALID_METHOD_SIG;

        pa_assert_se(dbus_message_get_args(call_info->message, NULL,
                                           DBUS_TYPE_STRING, &call_info->property_interface,
                                           DBUS_TYPE_INVALID));

        if (*call_info->property_interface) {
            if ((call_info->iface_entry = pa_hashmap_get(call_info->obj_entry->interfaces, call_info->property_interface)))
                return FOUND_GET_ALL;
            else
                return NO_SUCH_PROPERTY_INTERFACE;

        } else {
            pa_assert_se(call_info->iface_entry = pa_hashmap_first(call_info->obj_entry->interfaces));
            return FOUND_GET_ALL;
        }

    } else if (pa_streq(call_info->method, "Get")) {
        call_info->expected_method_sig = "ss";
        if (!pa_streq(call_info->method_sig, call_info->expected_method_sig))
            return INVALID_METHOD_SIG;

        pa_assert_se(dbus_message_get_args(call_info->message, NULL,
                                           DBUS_TYPE_STRING, &call_info->property_interface,
                                           DBUS_TYPE_STRING, &call_info->property,
                                           DBUS_TYPE_INVALID));

        if (*call_info->property_interface) {
            if (!(call_info->iface_entry = pa_hashmap_get(call_info->obj_entry->interfaces, call_info->property_interface)))
                return NO_SUCH_PROPERTY_INTERFACE;
            else if ((call_info->property_handler =
                        pa_hashmap_get(call_info->iface_entry->property_handlers, call_info->property)))
                return call_info->property_handler->get_cb ? FOUND_GET_PROPERTY : PROPERTY_ACCESS_DENIED;
            else
                return NO_SUCH_PROPERTY;

        } else
            return find_handler_by_property(call_info);

    } else if (pa_streq(call_info->method, "Set")) {
        DBusMessageIter msg_iter;

        call_info->expected_method_sig = "ssv";
        if (!pa_streq(call_info->method_sig, call_info->expected_method_sig))
            return INVALID_METHOD_SIG;

        pa_assert_se(dbus_message_iter_init(call_info->message, &msg_iter));

        dbus_message_iter_get_basic(&msg_iter, &call_info->property_interface);
        pa_assert_se(dbus_message_iter_next(&msg_iter));
        dbus_message_iter_get_basic(&msg_iter, &call_info->property);
        pa_assert_se(dbus_message_iter_next(&msg_iter));

        dbus_message_iter_recurse(&msg_iter, &call_info->variant_iter);

        pa_assert_se(call_info->property_sig = dbus_message_iter_get_signature(&call_info->variant_iter));

        if (*call_info->property_interface) {
            if (!(call_info->iface_entry = pa_hashmap_get(call_info->obj_entry->interfaces, call_info->property_interface)))
                return NO_SUCH_PROPERTY_INTERFACE;

            else if ((call_info->property_handler =
                        pa_hashmap_get(call_info->iface_entry->property_handlers, call_info->property))) {
                call_info->expected_property_sig = call_info->property_handler->type;

                if (pa_streq(call_info->property_sig, call_info->expected_property_sig))
                    return call_info->property_handler->set_cb ? FOUND_SET_PROPERTY : PROPERTY_ACCESS_DENIED;
                else
                    return INVALID_PROPERTY_SIG;

            } else
                return NO_SUCH_PROPERTY;

        } else
            return find_handler_by_property(call_info);

    } else
        pa_assert_not_reached();
}

static enum find_result_t find_handler(struct call_info *call_info) {
    pa_assert(call_info);

    if (call_info->interface) {
        if (pa_streq(call_info->interface, DBUS_INTERFACE_PROPERTIES))
            return find_handler_from_properties_call(call_info);

        else if (!(call_info->iface_entry = pa_hashmap_get(call_info->obj_entry->interfaces, call_info->interface)))
            return NO_SUCH_INTERFACE;

        else if ((call_info->method_handler = pa_hashmap_get(call_info->iface_entry->method_handlers, call_info->method))) {
            pa_assert_se(call_info->expected_method_sig = pa_hashmap_get(call_info->iface_entry->method_signatures, call_info->method));

            if (!pa_streq(call_info->method_sig, call_info->expected_method_sig))
                return INVALID_METHOD_SIG;

            return FOUND_METHOD;

        } else
            return NO_SUCH_METHOD;

    } else { /* The method call doesn't contain an interface. */
        if (pa_streq(call_info->method, "Get") || pa_streq(call_info->method, "Set") || pa_streq(call_info->method, "GetAll")) {
            if (find_handler_by_method(call_info) == FOUND_METHOD)
                /* The object has a method named Get, Set or GetAll in some other interface than .Properties. */
                return FOUND_METHOD;
            else
                /* Assume this is a .Properties call. */
                return find_handler_from_properties_call(call_info);

        } else /* This is not a .Properties call. */
            return find_handler_by_method(call_info);
    }
}

static DBusHandlerResult handle_message_cb(DBusConnection *connection, DBusMessage *message, void *user_data) {
    pa_dbus_protocol *p = user_data;
    struct call_info call_info;
    call_info.property_sig = NULL;

    pa_assert(connection);
    pa_assert(message);
    pa_assert(p);
    pa_assert(p->objects);

    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    pa_log_debug("Received message: destination = %s, interface = %s, member = %s",
                 dbus_message_get_path(message),
                 dbus_message_get_interface(message),
                 dbus_message_get_member(message));

    call_info.message = message;
    pa_assert_se(call_info.obj_entry = pa_hashmap_get(p->objects, dbus_message_get_path(message)));
    call_info.interface = dbus_message_get_interface(message);
    pa_assert_se(call_info.method = dbus_message_get_member(message));
    pa_assert_se(call_info.method_sig = dbus_message_get_signature(message));

    if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Introspectable", "Introspect") ||
        (!dbus_message_get_interface(message) && dbus_message_has_member(message, "Introspect"))) {
        pa_dbus_send_basic_value_reply(connection, message, DBUS_TYPE_STRING, &call_info.obj_entry->introspection);
        goto finish;
    }

    switch (find_handler(&call_info)) {
        case FOUND_GET_PROPERTY:
            call_info.property_handler->get_cb(connection, message, call_info.iface_entry->userdata);
            break;

        case FOUND_SET_PROPERTY:
            call_info.property_handler->set_cb(connection, message, &call_info.variant_iter, call_info.iface_entry->userdata);
            break;

        case FOUND_METHOD:
            call_info.method_handler->receive_cb(connection, message, call_info.iface_entry->userdata);
            break;

        case FOUND_GET_ALL:
            if (call_info.iface_entry->get_all_properties_cb)
                call_info.iface_entry->get_all_properties_cb(connection, message, call_info.iface_entry->userdata);
            else {
                DBusMessage *dummy_reply = NULL;
                DBusMessageIter msg_iter;
                DBusMessageIter dict_iter;

                pa_assert_se(dummy_reply = dbus_message_new_method_return(message));
                dbus_message_iter_init_append(dummy_reply, &msg_iter);
                pa_assert_se(dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter));
                pa_assert_se(dbus_message_iter_close_container(&msg_iter, &dict_iter));
                pa_assert_se(dbus_connection_send(connection, dummy_reply, NULL));
                dbus_message_unref(dummy_reply);
            }
            break;

        case PROPERTY_ACCESS_DENIED:
            pa_dbus_send_error(connection, message, DBUS_ERROR_ACCESS_DENIED,
                               "%s access denied for property %s", call_info.method, call_info.property);
            break;

        case NO_SUCH_METHOD:
            pa_dbus_send_error(connection, message, DBUS_ERROR_UNKNOWN_METHOD, "No such method: %s", call_info.method);
            break;

        case NO_SUCH_INTERFACE:
            pa_dbus_send_error(connection, message, PA_DBUS_ERROR_NO_SUCH_INTERFACE, "No such interface: %s", call_info.interface);
            break;

        case NO_SUCH_PROPERTY:
            pa_dbus_send_error(connection, message, PA_DBUS_ERROR_NO_SUCH_PROPERTY, "No such property: %s", call_info.property);
            break;

        case NO_SUCH_PROPERTY_INTERFACE:
            pa_dbus_send_error(connection, message, PA_DBUS_ERROR_NO_SUCH_INTERFACE, "No such property interface: %s", call_info.property_interface);
            break;

        case INVALID_METHOD_SIG:
            pa_dbus_send_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                               "Invalid signature for method %s: '%s'. Expected '%s'.",
                               call_info.method, call_info.method_sig, call_info.expected_method_sig);
            break;

        case INVALID_PROPERTY_SIG:
            pa_dbus_send_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                               "Invalid signature for property %s: '%s'. Expected '%s'.",
                               call_info.property, call_info.property_sig, call_info.expected_property_sig);
            break;

        default:
            pa_assert_not_reached();
    }

finish:
    if (call_info.property_sig)
        dbus_free(call_info.property_sig);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusObjectPathVTable vtable = {
    .unregister_function = NULL,
    .message_function = handle_message_cb,
    .dbus_internal_pad1 = NULL,
    .dbus_internal_pad2 = NULL,
    .dbus_internal_pad3 = NULL,
    .dbus_internal_pad4 = NULL
};

static void register_object(pa_dbus_protocol *p, struct object_entry *obj_entry) {
    struct connection_entry *conn_entry;
    void *state = NULL;

    pa_assert(p);
    pa_assert(obj_entry);

    PA_HASHMAP_FOREACH(conn_entry, p->connections, state)
        pa_assert_se(dbus_connection_register_object_path(conn_entry->connection, obj_entry->path, &vtable, p));
}

static pa_dbus_arg_info *copy_args(const pa_dbus_arg_info *src, unsigned n) {
    pa_dbus_arg_info *dst;
    unsigned i;

    if (n == 0)
        return NULL;

    pa_assert(src);

    dst = pa_xnew0(pa_dbus_arg_info, n);

    for (i = 0; i < n; ++i) {
        dst[i].name = pa_xstrdup(src[i].name);
        dst[i].type = pa_xstrdup(src[i].type);
        dst[i].direction = pa_xstrdup(src[i].direction);
    }

    return dst;
}

static void method_handler_free(pa_dbus_method_handler *h) {
    unsigned i;

    pa_assert(h);

    pa_xfree((char *) h->method_name);

    for (i = 0; i < h->n_arguments; ++i) {
        pa_xfree((char *) h->arguments[i].name);
        pa_xfree((char *) h->arguments[i].type);
        pa_xfree((char *) h->arguments[i].direction);
    }

    pa_xfree((pa_dbus_arg_info *) h->arguments);
    pa_xfree(h);
}

static pa_hashmap *create_method_handlers(const pa_dbus_interface_info *info) {
    pa_hashmap *handlers;
    unsigned i;

    pa_assert(info);
    pa_assert(info->method_handlers || info->n_method_handlers == 0);

    handlers = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, (pa_free_cb_t) method_handler_free);

    for (i = 0; i < info->n_method_handlers; ++i) {
        pa_dbus_method_handler *h = pa_xnew(pa_dbus_method_handler, 1);
        h->method_name = pa_xstrdup(info->method_handlers[i].method_name);
        h->arguments = copy_args(info->method_handlers[i].arguments, info->method_handlers[i].n_arguments);
        h->n_arguments = info->method_handlers[i].n_arguments;
        h->receive_cb = info->method_handlers[i].receive_cb;

        pa_hashmap_put(handlers, (char *) h->method_name, h);
    }

    return handlers;
}

static pa_hashmap *extract_method_signatures(pa_hashmap *method_handlers) {
    pa_hashmap *signatures = NULL;
    pa_dbus_method_handler *handler = NULL;
    void *state = NULL;
    pa_strbuf *sig_buf = NULL;
    unsigned i = 0;

    pa_assert(method_handlers);

    signatures = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, pa_xfree);

    PA_HASHMAP_FOREACH(handler, method_handlers, state) {
        sig_buf = pa_strbuf_new();

        for (i = 0; i < handler->n_arguments; ++i) {
            if (pa_streq(handler->arguments[i].direction, "in"))
                pa_strbuf_puts(sig_buf, handler->arguments[i].type);
        }

        pa_hashmap_put(signatures, (char *) handler->method_name, pa_strbuf_to_string_free(sig_buf));
    }

    return signatures;
}

static void property_handler_free(pa_dbus_property_handler *h) {
    pa_assert(h);

    pa_xfree((char *) h->property_name);
    pa_xfree((char *) h->type);

    pa_xfree(h);
}

static pa_hashmap *create_property_handlers(const pa_dbus_interface_info *info) {
    pa_hashmap *handlers;
    unsigned i = 0;

    pa_assert(info);
    pa_assert(info->property_handlers || info->n_property_handlers == 0);

    handlers = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, (pa_free_cb_t) property_handler_free);

    for (i = 0; i < info->n_property_handlers; ++i) {
        pa_dbus_property_handler *h = pa_xnew(pa_dbus_property_handler, 1);
        h->property_name = pa_xstrdup(info->property_handlers[i].property_name);
        h->type = pa_xstrdup(info->property_handlers[i].type);
        h->get_cb = info->property_handlers[i].get_cb;
        h->set_cb = info->property_handlers[i].set_cb;

        pa_hashmap_put(handlers, (char *) h->property_name, h);
    }

    return handlers;
}

static pa_dbus_signal_info *copy_signals(const pa_dbus_interface_info *info) {
    pa_dbus_signal_info *dst;
    unsigned i;

    pa_assert(info);

    if (info->n_signals == 0)
        return NULL;

    pa_assert(info->signals);

    dst = pa_xnew(pa_dbus_signal_info, info->n_signals);

    for (i = 0; i < info->n_signals; ++i) {
        dst[i].name = pa_xstrdup(info->signals[i].name);
        dst[i].arguments = copy_args(info->signals[i].arguments, info->signals[i].n_arguments);
        dst[i].n_arguments = info->signals[i].n_arguments;
    }

    return dst;
}

int pa_dbus_protocol_add_interface(pa_dbus_protocol *p,
                                   const char *path,
                                   const pa_dbus_interface_info *info,
                                   void *userdata) {
    struct object_entry *obj_entry;
    struct interface_entry *iface_entry;
    bool obj_entry_created = false;

    pa_assert(p);
    pa_assert(path);
    pa_assert(info);
    pa_assert(info->name);
    pa_assert(info->method_handlers || info->n_method_handlers == 0);
    pa_assert(info->property_handlers || info->n_property_handlers == 0);
    pa_assert(info->get_all_properties_cb || info->n_property_handlers == 0);
    pa_assert(info->signals || info->n_signals == 0);

    if (!(obj_entry = pa_hashmap_get(p->objects, path))) {
        obj_entry = pa_xnew(struct object_entry, 1);
        obj_entry->path = pa_xstrdup(path);
        obj_entry->interfaces = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        obj_entry->introspection = NULL;

        pa_hashmap_put(p->objects, obj_entry->path, obj_entry);
        obj_entry_created = true;
    }

    if (pa_hashmap_get(obj_entry->interfaces, info->name) != NULL)
        goto fail; /* The interface was already registered. */

    iface_entry = pa_xnew(struct interface_entry, 1);
    iface_entry->name = pa_xstrdup(info->name);
    iface_entry->method_handlers = create_method_handlers(info);
    iface_entry->method_signatures = extract_method_signatures(iface_entry->method_handlers);
    iface_entry->property_handlers = create_property_handlers(info);
    iface_entry->get_all_properties_cb = info->get_all_properties_cb;
    iface_entry->signals = copy_signals(info);
    iface_entry->n_signals = info->n_signals;
    iface_entry->userdata = userdata;
    pa_hashmap_put(obj_entry->interfaces, iface_entry->name, iface_entry);

    update_introspection(obj_entry);

    if (obj_entry_created)
        register_object(p, obj_entry);

    pa_log_debug("Interface %s added for object %s", iface_entry->name, obj_entry->path);

    return 0;

fail:
    return -1;
}

static void unregister_object(pa_dbus_protocol *p, struct object_entry *obj_entry) {
    struct connection_entry *conn_entry;
    void *state = NULL;

    pa_assert(p);
    pa_assert(obj_entry);

    PA_HASHMAP_FOREACH(conn_entry, p->connections, state)
        pa_assert_se(dbus_connection_unregister_object_path(conn_entry->connection, obj_entry->path));
}

int pa_dbus_protocol_remove_interface(pa_dbus_protocol *p, const char* path, const char* interface) {
    struct object_entry *obj_entry;
    struct interface_entry *iface_entry;
    unsigned i;

    pa_assert(p);
    pa_assert(path);
    pa_assert(interface);

    if (!(obj_entry = pa_hashmap_get(p->objects, path)))
        return -1;

    if (!(iface_entry = pa_hashmap_remove(obj_entry->interfaces, interface)))
        return -1;

    update_introspection(obj_entry);

    pa_log_debug("Interface %s removed from object %s", iface_entry->name, obj_entry->path);

    pa_xfree(iface_entry->name);
    pa_hashmap_free(iface_entry->method_signatures);
    pa_hashmap_free(iface_entry->method_handlers);
    pa_hashmap_free(iface_entry->property_handlers);

    for (i = 0; i < iface_entry->n_signals; ++i) {
        unsigned j;

        pa_xfree((char *) iface_entry->signals[i].name);

        for (j = 0; j < iface_entry->signals[i].n_arguments; ++j) {
            pa_xfree((char *) iface_entry->signals[i].arguments[j].name);
            pa_xfree((char *) iface_entry->signals[i].arguments[j].type);
            pa_assert(iface_entry->signals[i].arguments[j].direction == NULL);
        }

        pa_xfree((pa_dbus_arg_info *) iface_entry->signals[i].arguments);
    }

    pa_xfree(iface_entry->signals);
    pa_xfree(iface_entry);

    if (pa_hashmap_isempty(obj_entry->interfaces)) {
        unregister_object(p, obj_entry);

        pa_hashmap_remove(p->objects, path);
        pa_xfree(obj_entry->path);
        pa_hashmap_free(obj_entry->interfaces);
        pa_xfree(obj_entry->introspection);
        pa_xfree(obj_entry);
    }

    return 0;
}

static void register_all_objects(pa_dbus_protocol *p, DBusConnection *conn) {
    struct object_entry *obj_entry;
    void *state = NULL;

    pa_assert(p);
    pa_assert(conn);

    PA_HASHMAP_FOREACH(obj_entry, p->objects, state)
        pa_assert_se(dbus_connection_register_object_path(conn, obj_entry->path, &vtable, p));
}

static void signal_paths_entry_free(struct signal_paths_entry *e) {
    pa_assert(e);

    pa_xfree(e->signal);
    pa_idxset_free(e->paths, pa_xfree);
    pa_xfree(e);
}

int pa_dbus_protocol_register_connection(pa_dbus_protocol *p, DBusConnection *conn, pa_client *client) {
    struct connection_entry *conn_entry;

    pa_assert(p);
    pa_assert(conn);
    pa_assert(client);

    if (pa_hashmap_get(p->connections, conn))
        return -1; /* The connection was already registered. */

    register_all_objects(p, conn);

    conn_entry = pa_xnew(struct connection_entry, 1);
    conn_entry->connection = dbus_connection_ref(conn);
    conn_entry->client = client;
    conn_entry->listening_for_all_signals = false;
    conn_entry->all_signals_objects = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    conn_entry->listening_signals = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                                        (pa_free_cb_t) signal_paths_entry_free);

    pa_hashmap_put(p->connections, conn, conn_entry);

    return 0;
}

static void unregister_all_objects(pa_dbus_protocol *p, DBusConnection *conn) {
    struct object_entry *obj_entry;
    void *state = NULL;

    pa_assert(p);
    pa_assert(conn);

    PA_HASHMAP_FOREACH(obj_entry, p->objects, state)
        pa_assert_se(dbus_connection_unregister_object_path(conn, obj_entry->path));
}

static struct signal_paths_entry *signal_paths_entry_new(const char *signal_name) {
    struct signal_paths_entry *e = NULL;

    pa_assert(signal_name);

    e = pa_xnew0(struct signal_paths_entry, 1);
    e->signal = pa_xstrdup(signal_name);
    e->paths = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    return e;
}

int pa_dbus_protocol_unregister_connection(pa_dbus_protocol *p, DBusConnection *conn) {
    struct connection_entry *conn_entry = NULL;

    pa_assert(p);
    pa_assert(conn);

    if (!(conn_entry = pa_hashmap_remove(p->connections, conn)))
        return -1;

    unregister_all_objects(p, conn);

    dbus_connection_unref(conn_entry->connection);
    pa_idxset_free(conn_entry->all_signals_objects, pa_xfree);
    pa_hashmap_free(conn_entry->listening_signals);
    pa_xfree(conn_entry);

    return 0;
}

pa_client *pa_dbus_protocol_get_client(pa_dbus_protocol *p, DBusConnection *conn) {
    struct connection_entry *conn_entry;

    pa_assert(p);
    pa_assert(conn);

    if (!(conn_entry = pa_hashmap_get(p->connections, conn)))
        return NULL;

    return conn_entry->client;
}

void pa_dbus_protocol_add_signal_listener(
        pa_dbus_protocol *p,
        DBusConnection *conn,
        const char *signal_name,
        char **objects,
        unsigned n_objects) {
    struct connection_entry *conn_entry = NULL;
    struct signal_paths_entry *signal_paths_entry = NULL;
    unsigned i = 0;

    pa_assert(p);
    pa_assert(conn);
    pa_assert(objects || n_objects == 0);

    pa_assert_se((conn_entry = pa_hashmap_get(p->connections, conn)));

    /* all_signals_objects will either be emptied or replaced with new objects,
     * so we empty it here unconditionally. If listening_for_all_signals is
     * currently false, the idxset is empty already so this does nothing. */
    pa_idxset_remove_all(conn_entry->all_signals_objects, pa_xfree);

    if (signal_name) {
        conn_entry->listening_for_all_signals = false;

        /* Replace the old signal paths entry for this signal with a new
         * one. */
        pa_hashmap_remove_and_free(conn_entry->listening_signals, signal_name);
        signal_paths_entry = signal_paths_entry_new(signal_name);

        for (i = 0; i < n_objects; ++i)
            pa_idxset_put(signal_paths_entry->paths, pa_xstrdup(objects[i]), NULL);

        pa_hashmap_put(conn_entry->listening_signals, signal_paths_entry->signal, signal_paths_entry);

    } else {
        conn_entry->listening_for_all_signals = true;

        /* We're not interested in individual signals anymore, so let's empty
         * listening_signals. */
        pa_hashmap_remove_all(conn_entry->listening_signals);

        for (i = 0; i < n_objects; ++i)
            pa_idxset_put(conn_entry->all_signals_objects, pa_xstrdup(objects[i]), NULL);
    }
}

void pa_dbus_protocol_remove_signal_listener(pa_dbus_protocol *p, DBusConnection *conn, const char *signal_name) {
    struct connection_entry *conn_entry = NULL;
    struct signal_paths_entry *signal_paths_entry = NULL;

    pa_assert(p);
    pa_assert(conn);

    pa_assert_se((conn_entry = pa_hashmap_get(p->connections, conn)));

    if (signal_name) {
        if ((signal_paths_entry = pa_hashmap_remove(conn_entry->listening_signals, signal_name)))
            signal_paths_entry_free(signal_paths_entry);

    } else {
        conn_entry->listening_for_all_signals = false;
        pa_idxset_remove_all(conn_entry->all_signals_objects, pa_xfree);
        pa_hashmap_remove_all(conn_entry->listening_signals);
    }
}

void pa_dbus_protocol_send_signal(pa_dbus_protocol *p, DBusMessage *signal_msg) {
    struct connection_entry *conn_entry;
    struct signal_paths_entry *signal_paths_entry;
    void *state = NULL;
    DBusMessage *signal_copy;
    char *signal_string;

    pa_assert(p);
    pa_assert(signal_msg);
    pa_assert(dbus_message_get_type(signal_msg) == DBUS_MESSAGE_TYPE_SIGNAL);
    pa_assert(dbus_message_get_path(signal_msg));
    pa_assert(dbus_message_get_interface(signal_msg));
    pa_assert(dbus_message_get_member(signal_msg));

    signal_string = pa_sprintf_malloc("%s.%s", dbus_message_get_interface(signal_msg), dbus_message_get_member(signal_msg));

    PA_HASHMAP_FOREACH(conn_entry, p->connections, state) {
        if ((conn_entry->listening_for_all_signals /* Case 1: listening for all signals */
             && (pa_idxset_get_by_data(conn_entry->all_signals_objects, dbus_message_get_path(signal_msg), NULL)
                 || pa_idxset_isempty(conn_entry->all_signals_objects)))

            || (!conn_entry->listening_for_all_signals /* Case 2: not listening for all signals */
                && (signal_paths_entry = pa_hashmap_get(conn_entry->listening_signals, signal_string))
                && (pa_idxset_get_by_data(signal_paths_entry->paths, dbus_message_get_path(signal_msg), NULL)
                    || pa_idxset_isempty(signal_paths_entry->paths)))) {

            pa_assert_se(signal_copy = dbus_message_copy(signal_msg));
            pa_assert_se(dbus_connection_send(conn_entry->connection, signal_copy, NULL));
            dbus_message_unref(signal_copy);
        }
    }

    pa_xfree(signal_string);
}

const char **pa_dbus_protocol_get_extensions(pa_dbus_protocol *p, unsigned *n) {
    const char **extensions;
    const char *ext_name;
    void *state = NULL;
    unsigned i = 0;

    pa_assert(p);
    pa_assert(n);

    *n = pa_idxset_size(p->extensions);

    if (*n <= 0)
        return NULL;

    extensions = pa_xnew(const char *, *n);

    while ((ext_name = pa_idxset_iterate(p->extensions, &state, NULL)))
        extensions[i++] = ext_name;

    return extensions;
}

int pa_dbus_protocol_register_extension(pa_dbus_protocol *p, const char *name) {
    char *internal_name;

    pa_assert(p);
    pa_assert(name);

    internal_name = pa_xstrdup(name);

    if (pa_idxset_put(p->extensions, internal_name, NULL) < 0) {
        pa_xfree(internal_name);
        return -1;
    }

    pa_hook_fire(&p->hooks[PA_DBUS_PROTOCOL_HOOK_EXTENSION_REGISTERED], internal_name);

    return 0;
}

int pa_dbus_protocol_unregister_extension(pa_dbus_protocol *p, const char *name) {
    char *internal_name;

    pa_assert(p);
    pa_assert(name);

    if (!(internal_name = pa_idxset_remove_by_data(p->extensions, name, NULL)))
        return -1;

    pa_hook_fire(&p->hooks[PA_DBUS_PROTOCOL_HOOK_EXTENSION_UNREGISTERED], internal_name);

    pa_xfree(internal_name);

    return 0;
}

pa_hook_slot *pa_dbus_protocol_hook_connect(
        pa_dbus_protocol *p,
        pa_dbus_protocol_hook_t hook,
        pa_hook_priority_t prio,
        pa_hook_cb_t cb,
        void *data) {
    pa_assert(p);
    pa_assert(hook < PA_DBUS_PROTOCOL_HOOK_MAX);
    pa_assert(cb);

    return pa_hook_connect(&p->hooks[hook], prio, cb, data);
}
