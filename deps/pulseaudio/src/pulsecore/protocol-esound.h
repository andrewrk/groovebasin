#ifndef fooprotocolesoundhfoo
#define fooprotocolesoundhfoo

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

#include <pulsecore/core.h>
#include <pulsecore/ipacl.h>
#include <pulsecore/auth-cookie.h>
#include <pulsecore/iochannel.h>
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>

typedef struct pa_esound_protocol pa_esound_protocol;

typedef struct pa_esound_options {
    PA_REFCNT_DECLARE;

    pa_module *module;

    bool auth_anonymous;
    pa_ip_acl *auth_ip_acl;
    pa_auth_cookie *auth_cookie;

    char *default_sink, *default_source;
} pa_esound_options;

pa_esound_protocol* pa_esound_protocol_get(pa_core*core);
pa_esound_protocol* pa_esound_protocol_ref(pa_esound_protocol *p);
void pa_esound_protocol_unref(pa_esound_protocol *p);
void pa_esound_protocol_connect(pa_esound_protocol *p, pa_iochannel *io, pa_esound_options *o);
void pa_esound_protocol_disconnect(pa_esound_protocol *p, pa_module *m);

pa_esound_options* pa_esound_options_new(void);
pa_esound_options* pa_esound_options_ref(pa_esound_options *o);
void pa_esound_options_unref(pa_esound_options *o);
int pa_esound_options_parse(pa_esound_options *o, pa_core *c, pa_modargs *ma);

#endif
