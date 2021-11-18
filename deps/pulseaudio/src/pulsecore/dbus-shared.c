/***
  This file is part of PulseAudio.

  Copyright 2004-2006, 2009 Lennart Poettering
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

#include <pulse/xmalloc.h>

#include <pulsecore/shared.h>

#include "dbus-shared.h"

struct pa_dbus_connection {
    PA_REFCNT_DECLARE;

    pa_dbus_wrap_connection *connection;
    pa_core *core;
    const char *property_name;
};

static pa_dbus_connection* dbus_connection_new(pa_core *c, pa_dbus_wrap_connection *conn, const char *name) {
    pa_dbus_connection *pconn;

    pconn = pa_xnew(pa_dbus_connection, 1);
    PA_REFCNT_INIT(pconn);
    pconn->core = c;
    pconn->property_name = name;
    pconn->connection = conn;

    pa_assert_se(pa_shared_set(c, name, pconn) >= 0);

    return pconn;
}

pa_dbus_connection* pa_dbus_bus_get(pa_core *c, DBusBusType type, DBusError *error) {

    static const char *const prop_name[] = {
        [DBUS_BUS_SESSION] = "dbus-connection-session",
        [DBUS_BUS_SYSTEM] = "dbus-connection-system",
        [DBUS_BUS_STARTER] = "dbus-connection-starter"
    };
    pa_dbus_wrap_connection *conn;
    pa_dbus_connection *pconn;

    pa_assert(type == DBUS_BUS_SYSTEM || type == DBUS_BUS_SESSION || type == DBUS_BUS_STARTER);

    if ((pconn = pa_shared_get(c, prop_name[type])))
        return pa_dbus_connection_ref(pconn);

    if (!(conn = pa_dbus_wrap_connection_new(c->mainloop, true, type, error)))
        return NULL;

    return dbus_connection_new(c, conn, prop_name[type]);
}

DBusConnection* pa_dbus_connection_get(pa_dbus_connection *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) > 0);
    pa_assert(c->connection);

    return pa_dbus_wrap_connection_get(c->connection);
}

void pa_dbus_connection_unref(pa_dbus_connection *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) > 0);

    if (PA_REFCNT_DEC(c) > 0)
        return;

    pa_dbus_wrap_connection_free(c->connection);

    pa_assert_se(pa_shared_remove(c->core, c->property_name) >= 0);
    pa_xfree(c);
}

pa_dbus_connection* pa_dbus_connection_ref(pa_dbus_connection *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) > 0);

    PA_REFCNT_INC(c);

    return c;
}
