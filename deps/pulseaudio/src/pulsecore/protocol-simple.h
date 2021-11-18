#ifndef fooprotocolsimplehfoo
#define fooprotocolsimplehfoo

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

#include <pulsecore/socket-server.h>
#include <pulsecore/module.h>
#include <pulsecore/core.h>
#include <pulsecore/modargs.h>

typedef struct pa_simple_protocol pa_simple_protocol;

typedef struct pa_simple_options {
    PA_REFCNT_DECLARE;

    pa_module *module;

    char *default_sink, *default_source;

    pa_sample_spec sample_spec;
    pa_channel_map channel_map;

    bool record:1;
    bool playback:1;
} pa_simple_options;

pa_simple_protocol* pa_simple_protocol_get(pa_core*core);
pa_simple_protocol* pa_simple_protocol_ref(pa_simple_protocol *p);
void pa_simple_protocol_unref(pa_simple_protocol *p);
void pa_simple_protocol_connect(pa_simple_protocol *p, pa_iochannel *io, pa_simple_options *o);
void pa_simple_protocol_disconnect(pa_simple_protocol *p, pa_module *m);

pa_simple_options* pa_simple_options_new(void);
pa_simple_options* pa_simple_options_ref(pa_simple_options *o);
void pa_simple_options_unref(pa_simple_options *o);
int pa_simple_options_parse(pa_simple_options *o, pa_core *c, pa_modargs *ma);

#endif
