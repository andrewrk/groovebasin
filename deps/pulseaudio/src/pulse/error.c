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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <pulse/def.h>

#include <pulsecore/i18n.h>

#include "error.h"

const char*pa_strerror(int error) {

    static const char* const errortab[PA_ERR_MAX] = {
        [PA_OK] = N_("OK"),
        [PA_ERR_ACCESS] = N_("Access denied"),
        [PA_ERR_COMMAND] = N_("Unknown command"),
        [PA_ERR_INVALID] = N_("Invalid argument"),
        [PA_ERR_EXIST] = N_("Entity exists"),
        [PA_ERR_NOENTITY] = N_("No such entity"),
        [PA_ERR_CONNECTIONREFUSED] = N_("Connection refused"),
        [PA_ERR_PROTOCOL] = N_("Protocol error"),
        [PA_ERR_TIMEOUT] = N_("Timeout"),
        [PA_ERR_AUTHKEY] = N_("No authentication key"),
        [PA_ERR_INTERNAL] = N_("Internal error"),
        [PA_ERR_CONNECTIONTERMINATED] = N_("Connection terminated"),
        [PA_ERR_KILLED] = N_("Entity killed"),
        [PA_ERR_INVALIDSERVER] = N_("Invalid server"),
        [PA_ERR_MODINITFAILED] = N_("Module initialization failed"),
        [PA_ERR_BADSTATE] = N_("Bad state"),
        [PA_ERR_NODATA] = N_("No data"),
        [PA_ERR_VERSION] = N_("Incompatible protocol version"),
        [PA_ERR_TOOLARGE] = N_("Too large"),
        [PA_ERR_NOTSUPPORTED] = N_("Not supported"),
        [PA_ERR_UNKNOWN] = N_("Unknown error code"),
        [PA_ERR_NOEXTENSION] = N_("No such extension"),
        [PA_ERR_OBSOLETE] = N_("Obsolete functionality"),
        [PA_ERR_NOTIMPLEMENTED] = N_("Missing implementation"),
        [PA_ERR_FORKED] = N_("Client forked"),
        [PA_ERR_IO] = N_("Input/Output error"),
        [PA_ERR_BUSY] = N_("Device or resource busy")
    };

    pa_init_i18n();

    if (error < 0)
        error = -error;

    if (error >= PA_ERR_MAX)
        return NULL;

    return _(errortab[error]);
}
