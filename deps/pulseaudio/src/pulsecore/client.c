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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/xmalloc.h>
#include <pulse/util.h>

#include <pulsecore/core-subscribe.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>

#include "client.h"

pa_client_new_data* pa_client_new_data_init(pa_client_new_data *data) {
    pa_assert(data);

    memset(data, 0, sizeof(*data));
    data->proplist = pa_proplist_new();

    return data;
}

void pa_client_new_data_done(pa_client_new_data *data) {
    pa_assert(data);

    pa_proplist_free(data->proplist);
}

pa_client *pa_client_new(pa_core *core, pa_client_new_data *data) {
    pa_client *c;

    pa_core_assert_ref(core);
    pa_assert(data);

    if (pa_hook_fire(&core->hooks[PA_CORE_HOOK_CLIENT_NEW], data) < 0)
        return NULL;

    c = pa_xnew0(pa_client, 1);
    c->core = core;
    c->proplist = pa_proplist_copy(data->proplist);
    c->driver = pa_xstrdup(pa_path_get_filename(data->driver));
    c->module = data->module;

    c->sink_inputs = pa_idxset_new(NULL, NULL);
    c->source_outputs = pa_idxset_new(NULL, NULL);

    pa_assert_se(pa_idxset_put(core->clients, c, &c->index) >= 0);

    pa_log_info("Created %u \"%s\"", c->index, pa_strnull(pa_proplist_gets(c->proplist, PA_PROP_APPLICATION_NAME)));
    pa_subscription_post(core, PA_SUBSCRIPTION_EVENT_CLIENT|PA_SUBSCRIPTION_EVENT_NEW, c->index);

    pa_hook_fire(&core->hooks[PA_CORE_HOOK_CLIENT_PUT], c);

    pa_core_check_idle(core);

    return c;
}

void pa_client_free(pa_client *c) {
    pa_core *core;

    pa_assert(c);
    pa_assert(c->core);

    core = c->core;

    pa_hook_fire(&core->hooks[PA_CORE_HOOK_CLIENT_UNLINK], c);

    pa_idxset_remove_by_data(c->core->clients, c, NULL);

    pa_log_info("Freed %u \"%s\"", c->index, pa_strnull(pa_proplist_gets(c->proplist, PA_PROP_APPLICATION_NAME)));
    pa_subscription_post(c->core, PA_SUBSCRIPTION_EVENT_CLIENT|PA_SUBSCRIPTION_EVENT_REMOVE, c->index);

    pa_assert(pa_idxset_isempty(c->sink_inputs));
    pa_idxset_free(c->sink_inputs, NULL);
    pa_assert(pa_idxset_isempty(c->source_outputs));
    pa_idxset_free(c->source_outputs, NULL);

    pa_proplist_free(c->proplist);
    pa_xfree(c->driver);
    pa_xfree(c);

    pa_core_check_idle(core);
}

void pa_client_kill(pa_client *c) {
    pa_assert(c);

    if (!c->kill) {
        pa_log_warn("kill() operation not implemented for client %u", c->index);
        return;
    }

    c->kill(c);
}

void pa_client_set_name(pa_client *c, const char *name) {
    pa_assert(c);
    pa_assert(name);

    pa_log_info("Client %u changed name from \"%s\" to \"%s\"", c->index, pa_strnull(pa_proplist_gets(c->proplist, PA_PROP_APPLICATION_NAME)), name);
    pa_proplist_sets(c->proplist, PA_PROP_APPLICATION_NAME, name);

    pa_client_update_proplist(c, 0, NULL);
}

void pa_client_update_proplist(pa_client *c, pa_update_mode_t mode, pa_proplist *p) {
    pa_assert(c);

    if (p)
        pa_proplist_update(c->proplist, mode, p);

    pa_hook_fire(&c->core->hooks[PA_CORE_HOOK_CLIENT_PROPLIST_CHANGED], c);
    pa_subscription_post(c->core, PA_SUBSCRIPTION_EVENT_CLIENT|PA_SUBSCRIPTION_EVENT_CHANGE, c->index);
}

void pa_client_send_event(pa_client *c, const char *event, pa_proplist *data) {
    pa_proplist *pl = NULL;
    pa_client_send_event_hook_data hook_data;

    pa_assert(c);
    pa_assert(event);

    if (!c->send_event)
        return;

    if (!data)
        data = pl = pa_proplist_new();

    hook_data.client = c;
    hook_data.data = data;
    hook_data.event = event;

    if (pa_hook_fire(&c->core->hooks[PA_CORE_HOOK_CLIENT_SEND_EVENT], &hook_data) < 0)
        goto finish;

    c->send_event(c, event, data);

finish:

    if (pl)
        pa_proplist_free(pl);
}
