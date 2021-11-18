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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>

#include <pulse/xmalloc.h>

#include <pulsecore/cli.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/shared.h>

#include "protocol-cli.h"

/* Don't allow more than this many concurrent connections */
#define MAX_CONNECTIONS 25

struct pa_cli_protocol {
    PA_REFCNT_DECLARE;

    pa_core *core;
    pa_idxset *connections;
};

static void cli_unlink(pa_cli_protocol *p, pa_cli *c) {
    pa_assert(p);
    pa_assert(c);

    pa_idxset_remove_by_data(p->connections, c, NULL);
    pa_cli_free(c);
}

static void cli_eof_cb(pa_cli*c, void*userdata) {
    pa_cli_protocol *p = userdata;
    pa_assert(p);

    cli_unlink(p, c);
}

void pa_cli_protocol_connect(pa_cli_protocol *p, pa_iochannel *io, pa_module *m) {
    pa_cli *c;

    pa_assert(p);
    pa_assert(io);
    pa_assert(m);

    if (pa_idxset_size(p->connections)+1 > MAX_CONNECTIONS) {
        pa_log("Warning! Too many connections (%u), dropping incoming connection.", MAX_CONNECTIONS);
        pa_iochannel_free(io);
        return;
    }

    c = pa_cli_new(p->core, io, m);
    pa_cli_set_eof_callback(c, cli_eof_cb, p);

    pa_idxset_put(p->connections, c, NULL);
}

void pa_cli_protocol_disconnect(pa_cli_protocol *p, pa_module *m) {
    pa_cli *c;
    void *state = NULL;

    pa_assert(p);
    pa_assert(m);

    while ((c = pa_idxset_iterate(p->connections, &state, NULL)))
        if (pa_cli_get_module(c) == m)
            cli_unlink(p, c);
}

static pa_cli_protocol* cli_protocol_new(pa_core *c) {
    pa_cli_protocol *p;

    pa_assert(c);

    p = pa_xnew(pa_cli_protocol, 1);
    PA_REFCNT_INIT(p);
    p->core = c;
    p->connections = pa_idxset_new(NULL, NULL);

    pa_assert_se(pa_shared_set(c, "cli-protocol", p) >= 0);

    return p;
}

pa_cli_protocol* pa_cli_protocol_get(pa_core *c) {
    pa_cli_protocol *p;

    if ((p = pa_shared_get(c, "cli-protocol")))
        return pa_cli_protocol_ref(p);

    return cli_protocol_new(c);
}

pa_cli_protocol* pa_cli_protocol_ref(pa_cli_protocol *p) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    PA_REFCNT_INC(p);

    return p;
}

void pa_cli_protocol_unref(pa_cli_protocol *p) {
    pa_cli *c;
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    if (PA_REFCNT_DEC(p) > 0)
        return;

    while ((c = pa_idxset_first(p->connections, NULL)))
        cli_unlink(p, c);

    pa_idxset_free(p->connections, NULL);

    pa_assert_se(pa_shared_remove(p->core, "cli-protocol") >= 0);

    pa_xfree(p);
}
