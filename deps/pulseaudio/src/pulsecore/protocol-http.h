#ifndef fooprotocolhttphfoo
#define fooprotocolhttphfoo

/***
  This file is part of PulseAudio.

  Copyright 2005-2006 Lennart Poettering

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
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/iochannel.h>
#include <pulsecore/strlist.h>

typedef struct pa_http_protocol pa_http_protocol;

pa_http_protocol* pa_http_protocol_get(pa_core *core);
pa_http_protocol* pa_http_protocol_ref(pa_http_protocol *p);
void pa_http_protocol_unref(pa_http_protocol *p);
void pa_http_protocol_connect(pa_http_protocol *p, pa_iochannel *io, pa_module *m);
void pa_http_protocol_disconnect(pa_http_protocol *p, pa_module *m);

void pa_http_protocol_add_server_string(pa_http_protocol *p, const char *name);
void pa_http_protocol_remove_server_string(pa_http_protocol *p, const char *name);
pa_strlist *pa_http_protocol_servers(pa_http_protocol *p);

#endif
