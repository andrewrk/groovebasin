#ifndef fooprotocolclihfoo
#define fooprotocolclihfoo

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

#include <pulsecore/core.h>
#include <pulsecore/socket-server.h>
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>

typedef struct pa_cli_protocol pa_cli_protocol;

pa_cli_protocol* pa_cli_protocol_get(pa_core *core);
pa_cli_protocol* pa_cli_protocol_ref(pa_cli_protocol *p);
void pa_cli_protocol_unref(pa_cli_protocol *p);
void pa_cli_protocol_connect(pa_cli_protocol *p, pa_iochannel *io, pa_module *m);
void pa_cli_protocol_disconnect(pa_cli_protocol *o, pa_module *m);

#endif
