#ifndef fooprotocolnativehfoo
#define fooprotocolnativehfoo

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
#include <pulsecore/strlist.h>
#include <pulsecore/hook-list.h>
#include <pulsecore/pstream.h>
#include <pulsecore/tagstruct.h>

typedef struct pa_native_protocol pa_native_protocol;

typedef struct pa_native_connection pa_native_connection;

typedef struct pa_native_options {
    PA_REFCNT_DECLARE;

    pa_module *module;

    bool auth_anonymous;
    bool srbchannel;
    char *auth_group;
    pa_ip_acl *auth_ip_acl;
    pa_auth_cookie *auth_cookie;
} pa_native_options;

typedef enum pa_native_hook {
    PA_NATIVE_HOOK_SERVERS_CHANGED,
    PA_NATIVE_HOOK_CONNECTION_PUT,
    PA_NATIVE_HOOK_CONNECTION_UNLINK,
    PA_NATIVE_HOOK_MAX
} pa_native_hook_t;

pa_native_protocol* pa_native_protocol_get(pa_core *core);
pa_native_protocol* pa_native_protocol_ref(pa_native_protocol *p);
void pa_native_protocol_unref(pa_native_protocol *p);
void pa_native_protocol_connect(pa_native_protocol *p, pa_iochannel *io, pa_native_options *a);
void pa_native_protocol_disconnect(pa_native_protocol *p, pa_module *m);

pa_hook *pa_native_protocol_hooks(pa_native_protocol *p);

void pa_native_protocol_add_server_string(pa_native_protocol *p, const char *name);
void pa_native_protocol_remove_server_string(pa_native_protocol *p, const char *name);
pa_strlist *pa_native_protocol_servers(pa_native_protocol *p);

typedef int (*pa_native_protocol_ext_cb_t)(
        pa_native_protocol *p,
        pa_module *m,
        pa_native_connection *c,
        uint32_t tag,
        pa_tagstruct *t);

int pa_native_protocol_install_ext(pa_native_protocol *p, pa_module *m, pa_native_protocol_ext_cb_t cb);
void pa_native_protocol_remove_ext(pa_native_protocol *p, pa_module *m);

pa_pstream* pa_native_connection_get_pstream(pa_native_connection *c);
pa_client* pa_native_connection_get_client(pa_native_connection *c);

pa_native_options* pa_native_options_new(void);
pa_native_options* pa_native_options_ref(pa_native_options *o);
void pa_native_options_unref(pa_native_options *o);
int pa_native_options_parse(pa_native_options *o, pa_core *c, pa_modargs *ma);

#endif
