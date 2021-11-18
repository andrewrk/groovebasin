/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

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

#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>
#include <pulsecore/device-port.h>

#include "card.h"

const char *pa_available_to_string(pa_available_t available) {
    switch (available) {
        case PA_AVAILABLE_UNKNOWN:
            return "unknown";
        case PA_AVAILABLE_NO:
            return "no";
        case PA_AVAILABLE_YES:
            return "yes";
        default:
            pa_assert_not_reached();
    }
}

pa_card_profile *pa_card_profile_new(const char *name, const char *description, size_t extra) {
    pa_card_profile *c;

    pa_assert(name);

    c = pa_xmalloc0(PA_ALIGN(sizeof(pa_card_profile)) + extra);
    c->name = pa_xstrdup(name);
    c->description = pa_xstrdup(description);
    c->available = PA_AVAILABLE_UNKNOWN;

    return c;
}

void pa_card_profile_free(pa_card_profile *c) {
    pa_assert(c);

    pa_xfree(c->input_name);
    pa_xfree(c->output_name);
    pa_xfree(c->name);
    pa_xfree(c->description);
    pa_xfree(c);
}

void pa_card_profile_set_available(pa_card_profile *c, pa_available_t available) {
    pa_core *core;

    pa_assert(c);
    pa_assert(c->card); /* Modify member variable directly during creation instead of using this function */

    if (c->available == available)
        return;

    c->available = available;
    pa_log_debug("Setting card %s profile %s to availability status %s", c->card->name, c->name,
                 pa_available_to_string(available));

    /* Post subscriptions to the card which owns us */
    pa_assert_se(core = c->card->core);
    pa_subscription_post(core, PA_SUBSCRIPTION_EVENT_CARD|PA_SUBSCRIPTION_EVENT_CHANGE, c->card->index);

    if (c->card->linked)
        pa_hook_fire(&core->hooks[PA_CORE_HOOK_CARD_PROFILE_AVAILABLE_CHANGED], c);
}

pa_card_new_data* pa_card_new_data_init(pa_card_new_data *data) {
    pa_assert(data);

    memset(data, 0, sizeof(*data));
    data->proplist = pa_proplist_new();
    data->profiles = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, (pa_free_cb_t) pa_card_profile_free);
    data->ports = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, (pa_free_cb_t) pa_device_port_unref);
    return data;
}

void pa_card_new_data_set_name(pa_card_new_data *data, const char *name) {
    pa_assert(data);

    pa_xfree(data->name);
    data->name = pa_xstrdup(name);
}

void pa_card_new_data_set_preferred_port(pa_card_new_data *data, pa_direction_t direction, pa_device_port *port) {
    pa_assert(data);

    if (direction == PA_DIRECTION_INPUT)
        data->preferred_input_port = port;
    else
        data->preferred_output_port = port;
}

void pa_card_new_data_done(pa_card_new_data *data) {

    pa_assert(data);

    pa_proplist_free(data->proplist);

    if (data->profiles)
        pa_hashmap_free(data->profiles);

    if (data->ports)
        pa_hashmap_free(data->ports);

    pa_xfree(data->name);
}

pa_card *pa_card_new(pa_core *core, pa_card_new_data *data) {
    pa_card *c;
    const char *name;
    void *state;
    pa_card_profile *profile;
    pa_device_port *port;

    pa_core_assert_ref(core);
    pa_assert(data);
    pa_assert(data->name);
    pa_assert(data->profiles);
    pa_assert(!pa_hashmap_isempty(data->profiles));

    c = pa_xnew0(pa_card, 1);

    if (!(name = pa_namereg_register(core, data->name, PA_NAMEREG_CARD, c, data->namereg_fail))) {
        pa_xfree(c);
        return NULL;
    }

    pa_card_new_data_set_name(data, name);
    pa_hook_fire(&core->hooks[PA_CORE_HOOK_CARD_NEW], data);

    c->core = core;
    c->name = pa_xstrdup(data->name);
    c->proplist = pa_proplist_copy(data->proplist);
    c->driver = pa_xstrdup(pa_path_get_filename(data->driver));
    c->module = data->module;

    c->sinks = pa_idxset_new(NULL, NULL);
    c->sources = pa_idxset_new(NULL, NULL);

    /* As a minor optimization we just steal the list instead of
     * copying it here */
    pa_assert_se(c->profiles = data->profiles);
    data->profiles = NULL;
    pa_assert_se(c->ports = data->ports);
    data->ports = NULL;

    PA_HASHMAP_FOREACH(profile, c->profiles, state)
        profile->card = c;

    PA_HASHMAP_FOREACH(port, c->ports, state)
        port->card = c;

    c->preferred_input_port = data->preferred_input_port;
    c->preferred_output_port = data->preferred_output_port;

    pa_device_init_description(c->proplist, c);
    pa_device_init_icon(c->proplist, true);
    pa_device_init_intended_roles(c->proplist);

    return c;
}

void pa_card_choose_initial_profile(pa_card *card) {
    pa_card_profile *profile;
    void *state;
    pa_card_profile *best = NULL;

    pa_assert(card);

    /* By default, pick the highest priority profile that is not unavailable,
     * or if all profiles are unavailable, pick the profile with the highest
     * priority regardless of its availability. */

    pa_log_debug("Looking for initial profile for card %s", card->name);
    PA_HASHMAP_FOREACH(profile, card->profiles, state) {
        pa_log_debug("%s availability %s", profile->name, pa_available_to_string(profile->available));
        if (profile->available == PA_AVAILABLE_NO)
            continue;

        if (!best || profile->priority > best->priority)
            best = profile;
    }

    if (!best) {
        PA_HASHMAP_FOREACH(profile, card->profiles, state) {
            if (!best || profile->priority > best->priority)
                best = profile;
        }
    }
    pa_assert(best);

    card->active_profile = best;
    card->save_profile = false;
    pa_log_info("%s: active_profile: %s", card->name, card->active_profile->name);

    /* Let policy modules override the default. */
    pa_hook_fire(&card->core->hooks[PA_CORE_HOOK_CARD_CHOOSE_INITIAL_PROFILE], card);
}

void pa_card_put(pa_card *card) {
    pa_assert(card);

    pa_assert_se(pa_idxset_put(card->core->cards, card, &card->index) >= 0);
    card->linked = true;

    pa_log_info("Created %u \"%s\"", card->index, card->name);
    pa_hook_fire(&card->core->hooks[PA_CORE_HOOK_CARD_PUT], card);
    pa_subscription_post(card->core, PA_SUBSCRIPTION_EVENT_CARD|PA_SUBSCRIPTION_EVENT_NEW, card->index);
}

void pa_card_free(pa_card *c) {
    pa_core *core;

    pa_assert(c);
    pa_assert(c->core);

    core = c->core;

    if (c->linked) {
        pa_hook_fire(&core->hooks[PA_CORE_HOOK_CARD_UNLINK], c);

        pa_idxset_remove_by_data(c->core->cards, c, NULL);
        pa_log_info("Freed %u \"%s\"", c->index, c->name);
        pa_subscription_post(c->core, PA_SUBSCRIPTION_EVENT_CARD|PA_SUBSCRIPTION_EVENT_REMOVE, c->index);
    }

    pa_namereg_unregister(core, c->name);

    pa_assert(pa_idxset_isempty(c->sinks));
    pa_idxset_free(c->sinks, NULL);
    pa_assert(pa_idxset_isempty(c->sources));
    pa_idxset_free(c->sources, NULL);

    pa_hashmap_free(c->ports);

    if (c->profiles)
        pa_hashmap_free(c->profiles);

    pa_proplist_free(c->proplist);
    pa_xfree(c->driver);
    pa_xfree(c->name);
    pa_xfree(c);
}

void pa_card_add_profile(pa_card *c, pa_card_profile *profile) {
    pa_assert(c);
    pa_assert(profile);

    /* take ownership of the profile */
    pa_assert_se(pa_hashmap_put(c->profiles, profile->name, profile) >= 0);
    profile->card = c;

    pa_subscription_post(c->core, PA_SUBSCRIPTION_EVENT_CARD|PA_SUBSCRIPTION_EVENT_CHANGE, c->index);

    pa_hook_fire(&c->core->hooks[PA_CORE_HOOK_CARD_PROFILE_ADDED], profile);
}

static const char* profile_name_for_dir(pa_card_profile *cp, pa_direction_t dir) {
    if (dir == PA_DIRECTION_OUTPUT && cp->output_name)
        return cp->output_name;
    if (dir == PA_DIRECTION_INPUT && cp->input_name)
        return cp->input_name;
    return cp->name;
}

static void update_port_preferred_profile(pa_card *c) {
    pa_sink *sink;
    pa_source *source;
    uint32_t state;

    PA_IDXSET_FOREACH(sink, c->sinks, state)
        if (sink->active_port)
            pa_device_port_set_preferred_profile(sink->active_port, profile_name_for_dir(c->active_profile, PA_DIRECTION_OUTPUT));
    PA_IDXSET_FOREACH(source, c->sources, state)
        if (source->active_port)
            pa_device_port_set_preferred_profile(source->active_port, profile_name_for_dir(c->active_profile, PA_DIRECTION_INPUT));
}

int pa_card_set_profile(pa_card *c, pa_card_profile *profile, bool save) {
    int r;

    pa_assert(c);
    pa_assert(profile);
    pa_assert(profile->card == c);

    if (!c->set_profile) {
        pa_log_debug("set_profile() operation not implemented for card %u \"%s\"", c->index, c->name);
        return -PA_ERR_NOTIMPLEMENTED;
    }

    if (c->active_profile == profile) {
        if (save && !c->save_profile) {
            update_port_preferred_profile(c);
            c->save_profile = true;
        }
        return 0;
    }

    /* If we're setting the initial profile, we shouldn't call set_profile(),
     * because the implementations don't expect that (for historical reasons).
     * We should just set c->active_profile, and the implementations will
     * properly set up that profile after pa_card_put() has returned. It would
     * be probably good to change this so that also the initial profile can be
     * set up in set_profile(), but if set_profile() fails, that would need
     * some better handling than what we do here currently. */
    if (c->linked && (r = c->set_profile(c, profile)) < 0)
        return r;

    pa_log_debug("%s: active_profile: %s -> %s", c->name, c->active_profile->name, profile->name);
    c->active_profile = profile;
    c->save_profile = save;

    if (save)
        update_port_preferred_profile(c);

    if (c->linked) {
        pa_hook_fire(&c->core->hooks[PA_CORE_HOOK_CARD_PROFILE_CHANGED], c);
        pa_subscription_post(c->core, PA_SUBSCRIPTION_EVENT_CARD|PA_SUBSCRIPTION_EVENT_CHANGE, c->index);
    }

    return 0;
}

void pa_card_set_preferred_port(pa_card *c, pa_direction_t direction, pa_device_port *port) {
    pa_device_port *old_port;
    const char *old_port_str;
    const char *new_port_str;
    pa_card_preferred_port_changed_hook_data data;

    pa_assert(c);

    if (direction == PA_DIRECTION_INPUT) {
        old_port = c->preferred_input_port;
        old_port_str = c->preferred_input_port ? c->preferred_input_port->name : "(unset)";
    } else {
        old_port = c->preferred_output_port;
        old_port_str = c->preferred_output_port ? c->preferred_output_port->name : "(unset)";
    }

    if (port == old_port)
        return;

    new_port_str = port ? port->name : "(unset)";

    if (direction == PA_DIRECTION_INPUT) {
        c->preferred_input_port = port;
        pa_log_debug("%s: preferred_input_port: %s -> %s", c->name, old_port_str, new_port_str);
    } else {
        c->preferred_output_port = port;
        pa_log_debug("%s: preferred_output_port: %s -> %s", c->name, old_port_str, new_port_str);
    }

    data.card = c;
    data.direction = direction;
    pa_hook_fire(&c->core->hooks[PA_CORE_HOOK_CARD_PREFERRED_PORT_CHANGED], &data);
}

int pa_card_suspend(pa_card *c, bool suspend, pa_suspend_cause_t cause) {
    pa_sink *sink;
    pa_source *source;
    pa_suspend_cause_t suspend_cause;
    uint32_t idx;
    int ret = 0;

    pa_assert(c);
    pa_assert(cause != 0);

    suspend_cause = c->suspend_cause;

    if (suspend)
        suspend_cause |= cause;
    else
        suspend_cause &= ~cause;

    if (c->suspend_cause != suspend_cause) {
        pa_log_debug("Card suspend causes/state changed");
        c->suspend_cause = suspend_cause;
        pa_hook_fire(&c->core->hooks[PA_CORE_HOOK_CARD_SUSPEND_CHANGED], c);
    }

    PA_IDXSET_FOREACH(sink, c->sinks, idx) {
        int r;

        if ((r = pa_sink_suspend(sink, suspend, cause)) < 0)
            ret = r;
    }

    PA_IDXSET_FOREACH(source, c->sources, idx) {
        int r;

        if ((r = pa_source_suspend(source, suspend, cause)) < 0)
            ret = r;
    }

    return ret;
}
