#ifndef foodbussharedhfoo
#define foodbussharedhfoo

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

#include <dbus/dbus.h>

#include <pulsecore/core.h>
#include <pulsecore/dbus-util.h>

typedef struct pa_dbus_connection pa_dbus_connection;

/* return a pa_dbus_connection of the specified type for the given core,
 * like dbus_bus_get(), but integrates the connection with the pa_core */
pa_dbus_connection* pa_dbus_bus_get(pa_core *c, DBusBusType type, DBusError *error);

DBusConnection* pa_dbus_connection_get(pa_dbus_connection *conn);

pa_dbus_connection* pa_dbus_connection_ref(pa_dbus_connection *conn);
void pa_dbus_connection_unref(pa_dbus_connection *conn);

#endif
