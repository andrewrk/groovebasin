#ifndef fooprotocoldbushfoo
#define fooprotocoldbushfoo

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

#include <dbus/dbus.h>

#include <pulsecore/core.h>
#include <pulsecore/macro.h>

#define PA_DBUS_DEFAULT_PORT 24883
#define PA_DBUS_SOCKET_NAME "dbus-socket"

#define PA_DBUS_SYSTEM_SOCKET_PATH PA_SYSTEM_RUNTIME_PATH PA_PATH_SEP PA_DBUS_SOCKET_NAME

#define PA_DBUS_CORE_INTERFACE "org.PulseAudio.Core1"
#define PA_DBUS_CORE_OBJECT_PATH "/org/pulseaudio/core1"

#define PA_DBUS_ERROR_NO_SUCH_INTERFACE PA_DBUS_CORE_INTERFACE ".NoSuchInterfaceError"
#define PA_DBUS_ERROR_NO_SUCH_PROPERTY PA_DBUS_CORE_INTERFACE ".NoSuchPropertyError"
#define PA_DBUS_ERROR_NOT_FOUND PA_DBUS_CORE_INTERFACE ".NotFoundError"

/* Returns the default address of the server type in the escaped form. For
 * PA_SERVER_TYPE_NONE an empty string is returned. The caller frees the
 * string. */
char *pa_get_dbus_address_from_server_type(pa_server_type_t server_type);

typedef struct pa_dbus_protocol pa_dbus_protocol;

/* This function either creates a new pa_dbus_protocol object, or if one
 * already exists, increases the reference count. */
pa_dbus_protocol* pa_dbus_protocol_get(pa_core *c);

pa_dbus_protocol* pa_dbus_protocol_ref(pa_dbus_protocol *p);
void pa_dbus_protocol_unref(pa_dbus_protocol *p);

/* Called when a received message needs handling. Completely ignoring the
 * message isn't a good idea; if you can't handle the message, reply with an
 * error.
 *
 * The message signature is already checked against the introspection data, so
 * you don't have to do that yourself.
 *
 * All messages are method calls. */
typedef void (*pa_dbus_receive_cb_t)(DBusConnection *conn, DBusMessage *msg, void *userdata);

/* A specialized version of pa_dbus_receive_cb_t: the additional iterator
 * argument points to the element inside the new value variant.
 *
 * The new value signature is checked against the introspection data, so you
 * don't have to do that yourself. */
typedef void (*pa_dbus_set_property_cb_t)(DBusConnection *conn, DBusMessage *msg, DBusMessageIter *iter, void *userdata);

typedef struct pa_dbus_arg_info {
    const char *name;
    const char *type;
    const char *direction; /* NULL for signal arguments. */
} pa_dbus_arg_info;

typedef struct pa_dbus_signal_info {
    const char *name;
    const pa_dbus_arg_info *arguments; /* NULL, if the signal has no args. */
    unsigned n_arguments;
} pa_dbus_signal_info;

typedef struct pa_dbus_method_handler {
    const char *method_name;
    const pa_dbus_arg_info *arguments; /* NULL, if the method has no args. */
    unsigned n_arguments;
    pa_dbus_receive_cb_t receive_cb;
} pa_dbus_method_handler;

typedef struct pa_dbus_property_handler {
    const char *property_name;
    const char *type;

    /* The access mode for the property is determined by checking whether
     * get_cb or set_cb is NULL. */
    pa_dbus_receive_cb_t get_cb;
    pa_dbus_set_property_cb_t set_cb;
} pa_dbus_property_handler;

typedef struct pa_dbus_interface_info {
    const char* name;
    const pa_dbus_method_handler *method_handlers; /* NULL, if the interface has no methods. */
    unsigned n_method_handlers;
    const pa_dbus_property_handler *property_handlers; /* NULL, if the interface has no properties. */
    unsigned n_property_handlers;
    const pa_dbus_receive_cb_t get_all_properties_cb; /* May be NULL, in which case GetAll returns an error. */
    const pa_dbus_signal_info *signals; /* NULL, if the interface has no signals. */
    unsigned n_signals;
} pa_dbus_interface_info;

/* The following functions may only be called from the main thread. */

/* Registers the given interface to the given object path. It doesn't matter
 * whether or not the object has already been registered; if it is, then its
 * interface set is extended.
 *
 * Introspection requests are handled automatically.
 *
 * Userdata is passed to all the callbacks.
 *
 * Fails and returns a negative number if the object already has the interface
 * registered. */
int pa_dbus_protocol_add_interface(pa_dbus_protocol *p, const char *path, const pa_dbus_interface_info *info, void *userdata);

/* Returns a negative number if the given object doesn't have the given
 * interface registered. */
int pa_dbus_protocol_remove_interface(pa_dbus_protocol *p, const char* path, const char* interface);

/* Fails and returns a negative number if the connection is already
 * registered. */
int pa_dbus_protocol_register_connection(pa_dbus_protocol *p, DBusConnection *conn, pa_client *client);

/* Returns a negative number if the connection isn't registered. */
int pa_dbus_protocol_unregister_connection(pa_dbus_protocol *p, DBusConnection *conn);

/* Returns NULL if the connection isn't registered. */
pa_client *pa_dbus_protocol_get_client(pa_dbus_protocol *p, DBusConnection *conn);

/* Enables signal receiving for the given connection. The connection must have
 * been registered earlier. The signal string must contain both the signal
 * interface and the signal name, concatenated using a period as the separator.
 *
 * If the signal argument is NULL, all signals will be sent to the connection,
 * otherwise calling this function only adds the given signal to the list of
 * signals that will be delivered to the connection.
 *
 * The objects argument is a list of object paths. If the list is not empty,
 * only signals from the given objects are delivered. If this function is
 * called multiple time for the same connection and signal, the latest call
 * always replaces the previous object list. */
void pa_dbus_protocol_add_signal_listener(
        pa_dbus_protocol *p,
        DBusConnection *conn,
        const char *signal,
        char **objects,
        unsigned n_objects);

/* Disables the delivery of the signal for the given connection. The connection
 * must have been registered. If signal is NULL, all signals are disabled. If
 * signal is non-NULL and _add_signal_listener() was previously called with
 * NULL signal (causing all signals to be enabled), this function doesn't do
 * anything. Also, if the signal wasn't enabled before, this function doesn't
 * do anything in that case either. */
void pa_dbus_protocol_remove_signal_listener(pa_dbus_protocol *p, DBusConnection *conn, const char *signal);

/* Sends the given signal to all interested clients. By default no signals are
 * sent - clients have to explicitly to request signals by calling
 * .Core1.ListenForSignal. That method's handler then calls
 * pa_dbus_protocol_add_signal_listener(). */
void pa_dbus_protocol_send_signal(pa_dbus_protocol *p, DBusMessage *signal);

/* Returns an array of extension identifier strings. The strings pointers point
 * to the internal copies, so don't free the strings. The caller must free the
 * array, however. Also, do not save the returned pointer or any of the string
 * pointers, because the contained strings may be freed at any time. If you
 * need to save the array, copy it. */
const char **pa_dbus_protocol_get_extensions(pa_dbus_protocol *p, unsigned *n);

/* Modules that want to provide a D-Bus interface for clients should register
 * an identifier that the clients can use to check whether the additional
 * functionality is available.
 *
 * This function registers the extension with the given name. It is recommended
 * that the name follows the D-Bus interface naming convention, so that the
 * names remain unique in case there will be at some point in the future
 * extensions that aren't included with the main PulseAudio source tree. For
 * in-tree extensions the convention is to use the org.PulseAudio.Ext
 * namespace.
 *
 * It is suggested that the name contains a version number, and whenever the
 * extension interface is modified in non-backwards compatible way, the version
 * number is incremented.
 *
 * Fails and returns a negative number if the extension is already registered.
 */
int pa_dbus_protocol_register_extension(pa_dbus_protocol *p, const char *name);

/* Returns a negative number if the extension isn't registered. */
int pa_dbus_protocol_unregister_extension(pa_dbus_protocol *p, const char *name);

/* All hooks have the pa_dbus_protocol object as hook data. */
typedef enum pa_dbus_protocol_hook {
    PA_DBUS_PROTOCOL_HOOK_EXTENSION_REGISTERED, /* Extension name as call data. */
    PA_DBUS_PROTOCOL_HOOK_EXTENSION_UNREGISTERED, /* Extension name as call data. */
    PA_DBUS_PROTOCOL_HOOK_MAX
} pa_dbus_protocol_hook_t;

pa_hook_slot *pa_dbus_protocol_hook_connect(
        pa_dbus_protocol *p,
        pa_dbus_protocol_hook_t hook,
        pa_hook_priority_t prio,
        pa_hook_cb_t cb,
        void *data);

#endif
