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

#include <pulse/introspect.h>
#include <pulse/format.h>
#include <pulse/utf8.h>
#include <pulse/xmalloc.h>
#include <pulse/timeval.h>
#include <pulse/util.h>
#include <pulse/rtclock.h>
#include <pulse/internal.h>

#include <pulsecore/i18n.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/namereg.h>
#include <pulsecore/core-util.h>
#include <pulsecore/sample-util.h>
#include <pulsecore/mix.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/play-memblockq.h>
#include <pulsecore/flist.h>

#include "sink.h"

#define MAX_MIX_CHANNELS 32
#define MIX_BUFFER_LENGTH (pa_page_size())
#define ABSOLUTE_MIN_LATENCY (500)
#define ABSOLUTE_MAX_LATENCY (10*PA_USEC_PER_SEC)
#define DEFAULT_FIXED_LATENCY (250*PA_USEC_PER_MSEC)

PA_DEFINE_PUBLIC_CLASS(pa_sink, pa_msgobject);

struct pa_sink_volume_change {
    pa_usec_t at;
    pa_cvolume hw_volume;

    PA_LLIST_FIELDS(pa_sink_volume_change);
};

struct set_state_data {
    pa_sink_state_t state;
    pa_suspend_cause_t suspend_cause;
};

static void sink_free(pa_object *s);

static void pa_sink_volume_change_push(pa_sink *s);
static void pa_sink_volume_change_flush(pa_sink *s);
static void pa_sink_volume_change_rewind(pa_sink *s, size_t nbytes);

pa_sink_new_data* pa_sink_new_data_init(pa_sink_new_data *data) {
    pa_assert(data);

    pa_zero(*data);
    data->proplist = pa_proplist_new();
    data->ports = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, (pa_free_cb_t) pa_device_port_unref);

    return data;
}

void pa_sink_new_data_set_name(pa_sink_new_data *data, const char *name) {
    pa_assert(data);

    pa_xfree(data->name);
    data->name = pa_xstrdup(name);
}

void pa_sink_new_data_set_sample_spec(pa_sink_new_data *data, const pa_sample_spec *spec) {
    pa_assert(data);

    if ((data->sample_spec_is_set = !!spec))
        data->sample_spec = *spec;
}

void pa_sink_new_data_set_channel_map(pa_sink_new_data *data, const pa_channel_map *map) {
    pa_assert(data);

    if ((data->channel_map_is_set = !!map))
        data->channel_map = *map;
}

void pa_sink_new_data_set_alternate_sample_rate(pa_sink_new_data *data, const uint32_t alternate_sample_rate) {
    pa_assert(data);

    data->alternate_sample_rate_is_set = true;
    data->alternate_sample_rate = alternate_sample_rate;
}

void pa_sink_new_data_set_avoid_resampling(pa_sink_new_data *data, bool avoid_resampling) {
    pa_assert(data);

    data->avoid_resampling_is_set = true;
    data->avoid_resampling = avoid_resampling;
}

void pa_sink_new_data_set_volume(pa_sink_new_data *data, const pa_cvolume *volume) {
    pa_assert(data);

    if ((data->volume_is_set = !!volume))
        data->volume = *volume;
}

void pa_sink_new_data_set_muted(pa_sink_new_data *data, bool mute) {
    pa_assert(data);

    data->muted_is_set = true;
    data->muted = mute;
}

void pa_sink_new_data_set_port(pa_sink_new_data *data, const char *port) {
    pa_assert(data);

    pa_xfree(data->active_port);
    data->active_port = pa_xstrdup(port);
}

void pa_sink_new_data_done(pa_sink_new_data *data) {
    pa_assert(data);

    pa_proplist_free(data->proplist);

    if (data->ports)
        pa_hashmap_free(data->ports);

    pa_xfree(data->name);
    pa_xfree(data->active_port);
}

/* Called from main context */
static void reset_callbacks(pa_sink *s) {
    pa_assert(s);

    s->set_state_in_main_thread = NULL;
    s->set_state_in_io_thread = NULL;
    s->get_volume = NULL;
    s->set_volume = NULL;
    s->write_volume = NULL;
    s->get_mute = NULL;
    s->set_mute = NULL;
    s->request_rewind = NULL;
    s->update_requested_latency = NULL;
    s->set_port = NULL;
    s->get_formats = NULL;
    s->set_formats = NULL;
    s->reconfigure = NULL;
}

/* Called from main context */
pa_sink* pa_sink_new(
        pa_core *core,
        pa_sink_new_data *data,
        pa_sink_flags_t flags) {

    pa_sink *s;
    const char *name;
    char st[PA_SAMPLE_SPEC_SNPRINT_MAX], cm[PA_CHANNEL_MAP_SNPRINT_MAX];
    pa_source_new_data source_data;
    const char *dn;
    char *pt;

    pa_assert(core);
    pa_assert(data);
    pa_assert(data->name);
    pa_assert_ctl_context();

    s = pa_msgobject_new(pa_sink);

    if (!(name = pa_namereg_register(core, data->name, PA_NAMEREG_SINK, s, data->namereg_fail))) {
        pa_log_debug("Failed to register name %s.", data->name);
        pa_xfree(s);
        return NULL;
    }

    pa_sink_new_data_set_name(data, name);

    if (pa_hook_fire(&core->hooks[PA_CORE_HOOK_SINK_NEW], data) < 0) {
        pa_xfree(s);
        pa_namereg_unregister(core, name);
        return NULL;
    }

    /* FIXME, need to free s here on failure */

    pa_return_null_if_fail(!data->driver || pa_utf8_valid(data->driver));
    pa_return_null_if_fail(data->name && pa_utf8_valid(data->name) && data->name[0]);

    pa_return_null_if_fail(data->sample_spec_is_set && pa_sample_spec_valid(&data->sample_spec));

    if (!data->channel_map_is_set)
        pa_return_null_if_fail(pa_channel_map_init_auto(&data->channel_map, data->sample_spec.channels, PA_CHANNEL_MAP_DEFAULT));

    pa_return_null_if_fail(pa_channel_map_valid(&data->channel_map));
    pa_return_null_if_fail(data->channel_map.channels == data->sample_spec.channels);

    /* FIXME: There should probably be a general function for checking whether
     * the sink volume is allowed to be set, like there is for sink inputs. */
    pa_assert(!data->volume_is_set || !(flags & PA_SINK_SHARE_VOLUME_WITH_MASTER));

    if (!data->volume_is_set) {
        pa_cvolume_reset(&data->volume, data->sample_spec.channels);
        data->save_volume = false;
    }

    pa_return_null_if_fail(pa_cvolume_valid(&data->volume));
    pa_return_null_if_fail(pa_cvolume_compatible(&data->volume, &data->sample_spec));

    if (!data->muted_is_set)
        data->muted = false;

    if (data->card)
        pa_proplist_update(data->proplist, PA_UPDATE_MERGE, data->card->proplist);

    pa_device_init_description(data->proplist, data->card);
    pa_device_init_icon(data->proplist, true);
    pa_device_init_intended_roles(data->proplist);

    if (!data->active_port) {
        pa_device_port *p = pa_device_port_find_best(data->ports);
        if (p)
            pa_sink_new_data_set_port(data, p->name);
    }

    if (pa_hook_fire(&core->hooks[PA_CORE_HOOK_SINK_FIXATE], data) < 0) {
        pa_xfree(s);
        pa_namereg_unregister(core, name);
        return NULL;
    }

    s->parent.parent.free = sink_free;
    s->parent.process_msg = pa_sink_process_msg;

    s->core = core;
    s->state = PA_SINK_INIT;
    s->flags = flags;
    s->priority = 0;
    s->suspend_cause = data->suspend_cause;
    s->name = pa_xstrdup(name);
    s->proplist = pa_proplist_copy(data->proplist);
    s->driver = pa_xstrdup(pa_path_get_filename(data->driver));
    s->module = data->module;
    s->card = data->card;

    s->priority = pa_device_init_priority(s->proplist);

    s->sample_spec = data->sample_spec;
    s->channel_map = data->channel_map;
    s->default_sample_rate = s->sample_spec.rate;

    if (data->alternate_sample_rate_is_set)
        s->alternate_sample_rate = data->alternate_sample_rate;
    else
        s->alternate_sample_rate = s->core->alternate_sample_rate;

    if (data->avoid_resampling_is_set)
        s->avoid_resampling = data->avoid_resampling;
    else
        s->avoid_resampling = s->core->avoid_resampling;

    s->inputs = pa_idxset_new(NULL, NULL);
    s->n_corked = 0;
    s->input_to_master = NULL;

    s->reference_volume = s->real_volume = data->volume;
    pa_cvolume_reset(&s->soft_volume, s->sample_spec.channels);
    s->base_volume = PA_VOLUME_NORM;
    s->n_volume_steps = PA_VOLUME_NORM+1;
    s->muted = data->muted;
    s->refresh_volume = s->refresh_muted = false;

    reset_callbacks(s);
    s->userdata = NULL;

    s->asyncmsgq = NULL;

    /* As a minor optimization we just steal the list instead of
     * copying it here */
    s->ports = data->ports;
    data->ports = NULL;

    s->active_port = NULL;
    s->save_port = false;

    if (data->active_port)
        if ((s->active_port = pa_hashmap_get(s->ports, data->active_port)))
            s->save_port = data->save_port;

    /* Hopefully the active port has already been assigned in the previous call
       to pa_device_port_find_best, but better safe than sorry */
    if (!s->active_port)
        s->active_port = pa_device_port_find_best(s->ports);

    if (s->active_port)
        s->port_latency_offset = s->active_port->latency_offset;
    else
        s->port_latency_offset = 0;

    s->save_volume = data->save_volume;
    s->save_muted = data->save_muted;

    pa_silence_memchunk_get(
            &core->silence_cache,
            core->mempool,
            &s->silence,
            &s->sample_spec,
            0);

    s->thread_info.rtpoll = NULL;
    s->thread_info.inputs = pa_hashmap_new_full(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func, NULL,
                                                (pa_free_cb_t) pa_sink_input_unref);
    s->thread_info.soft_volume =  s->soft_volume;
    s->thread_info.soft_muted = s->muted;
    s->thread_info.state = s->state;
    s->thread_info.rewind_nbytes = 0;
    s->thread_info.rewind_requested = false;
    s->thread_info.max_rewind = 0;
    s->thread_info.max_request = 0;
    s->thread_info.requested_latency_valid = false;
    s->thread_info.requested_latency = 0;
    s->thread_info.min_latency = ABSOLUTE_MIN_LATENCY;
    s->thread_info.max_latency = ABSOLUTE_MAX_LATENCY;
    s->thread_info.fixed_latency = flags & PA_SINK_DYNAMIC_LATENCY ? 0 : DEFAULT_FIXED_LATENCY;

    PA_LLIST_HEAD_INIT(pa_sink_volume_change, s->thread_info.volume_changes);
    s->thread_info.volume_changes_tail = NULL;
    pa_sw_cvolume_divide(&s->thread_info.current_hw_volume, &s->real_volume, &s->soft_volume);
    s->thread_info.volume_change_safety_margin = core->deferred_volume_safety_margin_usec;
    s->thread_info.volume_change_extra_delay = core->deferred_volume_extra_delay_usec;
    s->thread_info.port_latency_offset = s->port_latency_offset;

    /* FIXME: This should probably be moved to pa_sink_put() */
    pa_assert_se(pa_idxset_put(core->sinks, s, &s->index) >= 0);

    if (s->card)
        pa_assert_se(pa_idxset_put(s->card->sinks, s, NULL) >= 0);

    pt = pa_proplist_to_string_sep(s->proplist, "\n    ");
    pa_log_info("Created sink %u \"%s\" with sample spec %s and channel map %s\n    %s",
                s->index,
                s->name,
                pa_sample_spec_snprint(st, sizeof(st), &s->sample_spec),
                pa_channel_map_snprint(cm, sizeof(cm), &s->channel_map),
                pt);
    pa_xfree(pt);

    pa_source_new_data_init(&source_data);
    pa_source_new_data_set_sample_spec(&source_data, &s->sample_spec);
    pa_source_new_data_set_channel_map(&source_data, &s->channel_map);
    pa_source_new_data_set_alternate_sample_rate(&source_data, s->alternate_sample_rate);
    pa_source_new_data_set_avoid_resampling(&source_data, s->avoid_resampling);
    source_data.name = pa_sprintf_malloc("%s.monitor", name);
    source_data.driver = data->driver;
    source_data.module = data->module;
    source_data.card = data->card;

    dn = pa_proplist_gets(s->proplist, PA_PROP_DEVICE_DESCRIPTION);
    pa_proplist_setf(source_data.proplist, PA_PROP_DEVICE_DESCRIPTION, "Monitor of %s", dn ? dn : s->name);
    pa_proplist_sets(source_data.proplist, PA_PROP_DEVICE_CLASS, "monitor");

    s->monitor_source = pa_source_new(core, &source_data,
                                      ((flags & PA_SINK_LATENCY) ? PA_SOURCE_LATENCY : 0) |
                                      ((flags & PA_SINK_DYNAMIC_LATENCY) ? PA_SOURCE_DYNAMIC_LATENCY : 0));

    pa_source_new_data_done(&source_data);

    if (!s->monitor_source) {
        pa_sink_unlink(s);
        pa_sink_unref(s);
        return NULL;
    }

    s->monitor_source->monitor_of = s;

    pa_source_set_latency_range(s->monitor_source, s->thread_info.min_latency, s->thread_info.max_latency);
    pa_source_set_fixed_latency(s->monitor_source, s->thread_info.fixed_latency);
    pa_source_set_max_rewind(s->monitor_source, s->thread_info.max_rewind);

    return s;
}

/* Called from main context */
static int sink_set_state(pa_sink *s, pa_sink_state_t state, pa_suspend_cause_t suspend_cause) {
    int ret = 0;
    bool state_changed;
    bool suspend_cause_changed;
    bool suspending;
    bool resuming;
    pa_sink_state_t old_state;
    pa_suspend_cause_t old_suspend_cause;

    pa_assert(s);
    pa_assert_ctl_context();

    state_changed = state != s->state;
    suspend_cause_changed = suspend_cause != s->suspend_cause;

    if (!state_changed && !suspend_cause_changed)
        return 0;

    suspending = PA_SINK_IS_OPENED(s->state) && state == PA_SINK_SUSPENDED;
    resuming = s->state == PA_SINK_SUSPENDED && PA_SINK_IS_OPENED(state);

    /* If we are resuming, suspend_cause must be 0. */
    pa_assert(!resuming || !suspend_cause);

    /* Here's something to think about: what to do with the suspend cause if
     * resuming the sink fails? The old suspend cause will be incorrect, so we
     * can't use that. On the other hand, if we set no suspend cause (as is the
     * case currently), then it looks strange to have a sink suspended without
     * any cause. It might be a good idea to add a new "resume failed" suspend
     * cause, or it might just add unnecessary complexity, given that the
     * current approach of not setting any suspend cause works well enough. */

    if (s->set_state_in_main_thread) {
        if ((ret = s->set_state_in_main_thread(s, state, suspend_cause)) < 0) {
            /* set_state_in_main_thread() is allowed to fail only when resuming. */
            pa_assert(resuming);

            /* If resuming fails, we set the state to SUSPENDED and
             * suspend_cause to 0. */
            state = PA_SINK_SUSPENDED;
            suspend_cause = 0;
            state_changed = false;
            suspend_cause_changed = suspend_cause != s->suspend_cause;
            resuming = false;

            /* We know the state isn't changing. If the suspend cause isn't
             * changing either, then there's nothing more to do. */
            if (!suspend_cause_changed)
                return ret;
        }
    }

    if (s->asyncmsgq) {
        struct set_state_data data = { .state = state, .suspend_cause = suspend_cause };

        if ((ret = pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_SET_STATE, &data, 0, NULL)) < 0) {
            /* SET_STATE is allowed to fail only when resuming. */
            pa_assert(resuming);

            if (s->set_state_in_main_thread)
                s->set_state_in_main_thread(s, PA_SINK_SUSPENDED, 0);

            /* If resuming fails, we set the state to SUSPENDED and
             * suspend_cause to 0. */
            state = PA_SINK_SUSPENDED;
            suspend_cause = 0;
            state_changed = false;
            suspend_cause_changed = suspend_cause != s->suspend_cause;
            resuming = false;

            /* We know the state isn't changing. If the suspend cause isn't
             * changing either, then there's nothing more to do. */
            if (!suspend_cause_changed)
                return ret;
        }
    }

    old_suspend_cause = s->suspend_cause;
    if (suspend_cause_changed) {
        char old_cause_buf[PA_SUSPEND_CAUSE_TO_STRING_BUF_SIZE];
        char new_cause_buf[PA_SUSPEND_CAUSE_TO_STRING_BUF_SIZE];

        pa_log_debug("%s: suspend_cause: %s -> %s", s->name, pa_suspend_cause_to_string(s->suspend_cause, old_cause_buf),
                     pa_suspend_cause_to_string(suspend_cause, new_cause_buf));
        s->suspend_cause = suspend_cause;
    }

    old_state = s->state;
    if (state_changed) {
        pa_log_debug("%s: state: %s -> %s", s->name, pa_sink_state_to_string(s->state), pa_sink_state_to_string(state));
        s->state = state;

        /* If we enter UNLINKED state, then we don't send change notifications.
         * pa_sink_unlink() will send unlink notifications instead. */
        if (state != PA_SINK_UNLINKED) {
            pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_STATE_CHANGED], s);
            pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK | PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
        }
    }

    if (suspending || resuming || suspend_cause_changed) {
        pa_sink_input *i;
        uint32_t idx;

        /* We're suspending or resuming, tell everyone about it */

        PA_IDXSET_FOREACH(i, s->inputs, idx)
            if (s->state == PA_SINK_SUSPENDED &&
                (i->flags & PA_SINK_INPUT_KILL_ON_SUSPEND))
                pa_sink_input_kill(i);
            else if (i->suspend)
                i->suspend(i, old_state, old_suspend_cause);
    }

    if ((suspending || resuming || suspend_cause_changed) && s->monitor_source && state != PA_SINK_UNLINKED)
        pa_source_sync_suspend(s->monitor_source);

    return ret;
}

void pa_sink_set_get_volume_callback(pa_sink *s, pa_sink_cb_t cb) {
    pa_assert(s);

    s->get_volume = cb;
}

void pa_sink_set_set_volume_callback(pa_sink *s, pa_sink_cb_t cb) {
    pa_sink_flags_t flags;

    pa_assert(s);
    pa_assert(!s->write_volume || cb);

    s->set_volume = cb;

    /* Save the current flags so we can tell if they've changed */
    flags = s->flags;

    if (cb) {
        /* The sink implementor is responsible for setting decibel volume support */
        s->flags |= PA_SINK_HW_VOLUME_CTRL;
    } else {
        s->flags &= ~PA_SINK_HW_VOLUME_CTRL;
        /* See note below in pa_sink_put() about volume sharing and decibel volumes */
        pa_sink_enable_decibel_volume(s, !(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER));
    }

    /* If the flags have changed after init, let any clients know via a change event */
    if (s->state != PA_SINK_INIT && flags != s->flags)
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
}

void pa_sink_set_write_volume_callback(pa_sink *s, pa_sink_cb_t cb) {
    pa_sink_flags_t flags;

    pa_assert(s);
    pa_assert(!cb || s->set_volume);

    s->write_volume = cb;

    /* Save the current flags so we can tell if they've changed */
    flags = s->flags;

    if (cb)
        s->flags |= PA_SINK_DEFERRED_VOLUME;
    else
        s->flags &= ~PA_SINK_DEFERRED_VOLUME;

    /* If the flags have changed after init, let any clients know via a change event */
    if (s->state != PA_SINK_INIT && flags != s->flags)
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
}

void pa_sink_set_get_mute_callback(pa_sink *s, pa_sink_get_mute_cb_t cb) {
    pa_assert(s);

    s->get_mute = cb;
}

void pa_sink_set_set_mute_callback(pa_sink *s, pa_sink_cb_t cb) {
    pa_sink_flags_t flags;

    pa_assert(s);

    s->set_mute = cb;

    /* Save the current flags so we can tell if they've changed */
    flags = s->flags;

    if (cb)
        s->flags |= PA_SINK_HW_MUTE_CTRL;
    else
        s->flags &= ~PA_SINK_HW_MUTE_CTRL;

    /* If the flags have changed after init, let any clients know via a change event */
    if (s->state != PA_SINK_INIT && flags != s->flags)
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
}

static void enable_flat_volume(pa_sink *s, bool enable) {
    pa_sink_flags_t flags;

    pa_assert(s);

    /* Always follow the overall user preference here */
    enable = enable && s->core->flat_volumes;

    /* Save the current flags so we can tell if they've changed */
    flags = s->flags;

    if (enable)
        s->flags |= PA_SINK_FLAT_VOLUME;
    else
        s->flags &= ~PA_SINK_FLAT_VOLUME;

    /* If the flags have changed after init, let any clients know via a change event */
    if (s->state != PA_SINK_INIT && flags != s->flags)
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
}

void pa_sink_enable_decibel_volume(pa_sink *s, bool enable) {
    pa_sink_flags_t flags;

    pa_assert(s);

    /* Save the current flags so we can tell if they've changed */
    flags = s->flags;

    if (enable) {
        s->flags |= PA_SINK_DECIBEL_VOLUME;
        enable_flat_volume(s, true);
    } else {
        s->flags &= ~PA_SINK_DECIBEL_VOLUME;
        enable_flat_volume(s, false);
    }

    /* If the flags have changed after init, let any clients know via a change event */
    if (s->state != PA_SINK_INIT && flags != s->flags)
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
}

/* Called from main context */
void pa_sink_put(pa_sink* s) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    pa_assert(s->state == PA_SINK_INIT);
    pa_assert(!(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER) || pa_sink_is_filter(s));

    /* The following fields must be initialized properly when calling _put() */
    pa_assert(s->asyncmsgq);
    pa_assert(s->thread_info.min_latency <= s->thread_info.max_latency);

    /* Generally, flags should be initialized via pa_sink_new(). As a
     * special exception we allow some volume related flags to be set
     * between _new() and _put() by the callback setter functions above.
     *
     * Thus we implement a couple safeguards here which ensure the above
     * setters were used (or at least the implementor made manual changes
     * in a compatible way).
     *
     * Note: All of these flags set here can change over the life time
     * of the sink. */
    pa_assert(!(s->flags & PA_SINK_HW_VOLUME_CTRL) || s->set_volume);
    pa_assert(!(s->flags & PA_SINK_DEFERRED_VOLUME) || s->write_volume);
    pa_assert(!(s->flags & PA_SINK_HW_MUTE_CTRL) || s->set_mute);

    /* XXX: Currently decibel volume is disabled for all sinks that use volume
     * sharing. When the master sink supports decibel volume, it would be good
     * to have the flag also in the filter sink, but currently we don't do that
     * so that the flags of the filter sink never change when it's moved from
     * a master sink to another. One solution for this problem would be to
     * remove user-visible volume altogether from filter sinks when volume
     * sharing is used, but the current approach was easier to implement... */
    /* We always support decibel volumes in software, otherwise we leave it to
     * the sink implementor to set this flag as needed.
     *
     * Note: This flag can also change over the life time of the sink. */
    if (!(s->flags & PA_SINK_HW_VOLUME_CTRL) && !(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)) {
        pa_sink_enable_decibel_volume(s, true);
        s->soft_volume = s->reference_volume;
    }

    /* If the sink implementor support DB volumes by itself, we should always
     * try and enable flat volumes too */
    if ((s->flags & PA_SINK_DECIBEL_VOLUME))
        enable_flat_volume(s, true);

    if (s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER) {
        pa_sink *root_sink = pa_sink_get_master(s);

        pa_assert(root_sink);

        s->reference_volume = root_sink->reference_volume;
        pa_cvolume_remap(&s->reference_volume, &root_sink->channel_map, &s->channel_map);

        s->real_volume = root_sink->real_volume;
        pa_cvolume_remap(&s->real_volume, &root_sink->channel_map, &s->channel_map);
    } else
        /* We assume that if the sink implementor changed the default
         * volume they did so in real_volume, because that is the usual
         * place where they are supposed to place their changes.  */
        s->reference_volume = s->real_volume;

    s->thread_info.soft_volume = s->soft_volume;
    s->thread_info.soft_muted = s->muted;
    pa_sw_cvolume_divide(&s->thread_info.current_hw_volume, &s->real_volume, &s->soft_volume);

    pa_assert((s->flags & PA_SINK_HW_VOLUME_CTRL)
              || (s->base_volume == PA_VOLUME_NORM
                  && ((s->flags & PA_SINK_DECIBEL_VOLUME || (s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)))));
    pa_assert(!(s->flags & PA_SINK_DECIBEL_VOLUME) || s->n_volume_steps == PA_VOLUME_NORM+1);
    pa_assert(!(s->flags & PA_SINK_DYNAMIC_LATENCY) == !(s->thread_info.fixed_latency == 0));
    pa_assert(!(s->flags & PA_SINK_LATENCY) == !(s->monitor_source->flags & PA_SOURCE_LATENCY));
    pa_assert(!(s->flags & PA_SINK_DYNAMIC_LATENCY) == !(s->monitor_source->flags & PA_SOURCE_DYNAMIC_LATENCY));

    pa_assert(s->monitor_source->thread_info.fixed_latency == s->thread_info.fixed_latency);
    pa_assert(s->monitor_source->thread_info.min_latency == s->thread_info.min_latency);
    pa_assert(s->monitor_source->thread_info.max_latency == s->thread_info.max_latency);

    if (s->suspend_cause)
        pa_assert_se(sink_set_state(s, PA_SINK_SUSPENDED, s->suspend_cause) == 0);
    else
        pa_assert_se(sink_set_state(s, PA_SINK_IDLE, 0) == 0);

    pa_source_put(s->monitor_source);

    pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK | PA_SUBSCRIPTION_EVENT_NEW, s->index);
    pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_PUT], s);

    /* It's good to fire the SINK_PUT hook before updating the default sink,
     * because module-switch-on-connect will set the new sink as the default
     * sink, and if we were to call pa_core_update_default_sink() before that,
     * the default sink might change twice, causing unnecessary stream moving. */

    pa_core_update_default_sink(s->core);

    pa_core_move_streams_to_newly_available_preferred_sink(s->core, s);
}

/* Called from main context */
void pa_sink_unlink(pa_sink* s) {
    bool linked;
    pa_sink_input *i, PA_UNUSED *j = NULL;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    /* Please note that pa_sink_unlink() does more than simply
     * reversing pa_sink_put(). It also undoes the registrations
     * already done in pa_sink_new()! */

    if (s->unlink_requested)
        return;

    s->unlink_requested = true;

    linked = PA_SINK_IS_LINKED(s->state);

    if (linked)
        pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_UNLINK], s);

    if (s->state != PA_SINK_UNLINKED)
        pa_namereg_unregister(s->core, s->name);
    pa_idxset_remove_by_data(s->core->sinks, s, NULL);

    pa_core_update_default_sink(s->core);

    if (linked && s->core->rescue_streams)
	pa_sink_move_streams_to_default_sink(s->core, s, false);

    if (s->card)
        pa_idxset_remove_by_data(s->card->sinks, s, NULL);

    while ((i = pa_idxset_first(s->inputs, NULL))) {
        pa_assert(i != j);
        pa_sink_input_kill(i);
        j = i;
    }

    if (linked)
        /* It's important to keep the suspend cause unchanged when unlinking,
         * because if we remove the SESSION suspend cause here, the alsa sink
         * will sync its volume with the hardware while another user is
         * active, messing up the volume for that other user. */
        sink_set_state(s, PA_SINK_UNLINKED, s->suspend_cause);
    else
        s->state = PA_SINK_UNLINKED;

    reset_callbacks(s);

    if (s->monitor_source)
        pa_source_unlink(s->monitor_source);

    if (linked) {
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK | PA_SUBSCRIPTION_EVENT_REMOVE, s->index);
        pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_UNLINK_POST], s);
    }
}

/* Called from main context */
static void sink_free(pa_object *o) {
    pa_sink *s = PA_SINK(o);

    pa_assert(s);
    pa_assert_ctl_context();
    pa_assert(pa_sink_refcnt(s) == 0);
    pa_assert(!PA_SINK_IS_LINKED(s->state));

    pa_log_info("Freeing sink %u \"%s\"", s->index, s->name);

    pa_sink_volume_change_flush(s);

    if (s->monitor_source) {
        pa_source_unref(s->monitor_source);
        s->monitor_source = NULL;
    }

    pa_idxset_free(s->inputs, NULL);
    pa_hashmap_free(s->thread_info.inputs);

    if (s->silence.memblock)
        pa_memblock_unref(s->silence.memblock);

    pa_xfree(s->name);
    pa_xfree(s->driver);

    if (s->proplist)
        pa_proplist_free(s->proplist);

    if (s->ports)
        pa_hashmap_free(s->ports);

    pa_xfree(s);
}

/* Called from main context, and not while the IO thread is active, please */
void pa_sink_set_asyncmsgq(pa_sink *s, pa_asyncmsgq *q) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    s->asyncmsgq = q;

    if (s->monitor_source)
        pa_source_set_asyncmsgq(s->monitor_source, q);
}

/* Called from main context, and not while the IO thread is active, please */
void pa_sink_update_flags(pa_sink *s, pa_sink_flags_t mask, pa_sink_flags_t value) {
    pa_sink_flags_t old_flags;
    pa_sink_input *input;
    uint32_t idx;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    /* For now, allow only a minimal set of flags to be changed. */
    pa_assert((mask & ~(PA_SINK_DYNAMIC_LATENCY|PA_SINK_LATENCY)) == 0);

    old_flags = s->flags;
    s->flags = (s->flags & ~mask) | (value & mask);

    if (s->flags == old_flags)
        return;

    if ((s->flags & PA_SINK_LATENCY) != (old_flags & PA_SINK_LATENCY))
        pa_log_debug("Sink %s: LATENCY flag %s.", s->name, (s->flags & PA_SINK_LATENCY) ? "enabled" : "disabled");

    if ((s->flags & PA_SINK_DYNAMIC_LATENCY) != (old_flags & PA_SINK_DYNAMIC_LATENCY))
        pa_log_debug("Sink %s: DYNAMIC_LATENCY flag %s.",
                     s->name, (s->flags & PA_SINK_DYNAMIC_LATENCY) ? "enabled" : "disabled");

    pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK | PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
    pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_FLAGS_CHANGED], s);

    if (s->monitor_source)
        pa_source_update_flags(s->monitor_source,
                               ((mask & PA_SINK_LATENCY) ? PA_SOURCE_LATENCY : 0) |
                               ((mask & PA_SINK_DYNAMIC_LATENCY) ? PA_SOURCE_DYNAMIC_LATENCY : 0),
                               ((value & PA_SINK_LATENCY) ? PA_SOURCE_LATENCY : 0) |
                               ((value & PA_SINK_DYNAMIC_LATENCY) ? PA_SOURCE_DYNAMIC_LATENCY : 0));

    PA_IDXSET_FOREACH(input, s->inputs, idx) {
        if (input->origin_sink)
            pa_sink_update_flags(input->origin_sink, mask, value);
    }
}

/* Called from IO context, or before _put() from main context */
void pa_sink_set_rtpoll(pa_sink *s, pa_rtpoll *p) {
    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);

    s->thread_info.rtpoll = p;

    if (s->monitor_source)
        pa_source_set_rtpoll(s->monitor_source, p);
}

/* Called from main context */
int pa_sink_update_status(pa_sink*s) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    if (s->state == PA_SINK_SUSPENDED)
        return 0;

    return sink_set_state(s, pa_sink_used_by(s) ? PA_SINK_RUNNING : PA_SINK_IDLE, 0);
}

/* Called from main context */
int pa_sink_suspend(pa_sink *s, bool suspend, pa_suspend_cause_t cause) {
    pa_suspend_cause_t merged_cause;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));
    pa_assert(cause != 0);

    if (suspend)
        merged_cause = s->suspend_cause | cause;
    else
        merged_cause = s->suspend_cause & ~cause;

    if (merged_cause)
        return sink_set_state(s, PA_SINK_SUSPENDED, merged_cause);
    else
        return sink_set_state(s, pa_sink_used_by(s) ? PA_SINK_RUNNING : PA_SINK_IDLE, 0);
}

/* Called from main context */
pa_queue *pa_sink_move_all_start(pa_sink *s, pa_queue *q) {
    pa_sink_input *i, *n;
    uint32_t idx;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    if (!q)
        q = pa_queue_new();

    for (i = PA_SINK_INPUT(pa_idxset_first(s->inputs, &idx)); i; i = n) {
        n = PA_SINK_INPUT(pa_idxset_next(s->inputs, &idx));

        pa_sink_input_ref(i);

        if (pa_sink_input_start_move(i) >= 0)
            pa_queue_push(q, i);
        else
            pa_sink_input_unref(i);
    }

    return q;
}

/* Called from main context */
void pa_sink_move_all_finish(pa_sink *s, pa_queue *q, bool save) {
    pa_sink_input *i;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));
    pa_assert(q);

    while ((i = PA_SINK_INPUT(pa_queue_pop(q)))) {
        if (PA_SINK_INPUT_IS_LINKED(i->state)) {
            if (pa_sink_input_finish_move(i, s, save) < 0)
                pa_sink_input_fail_move(i);

        }
        pa_sink_input_unref(i);
    }

    pa_queue_free(q, NULL);
}

/* Called from main context */
void pa_sink_move_all_fail(pa_queue *q) {
    pa_sink_input *i;

    pa_assert_ctl_context();
    pa_assert(q);

    while ((i = PA_SINK_INPUT(pa_queue_pop(q)))) {
        pa_sink_input_fail_move(i);
        pa_sink_input_unref(i);
    }

    pa_queue_free(q, NULL);
}

 /* Called from IO thread context */
size_t pa_sink_process_input_underruns(pa_sink *s, size_t left_to_play) {
    pa_sink_input *i;
    void *state = NULL;
    size_t result = 0;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);

    PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state) {
        size_t uf = i->thread_info.underrun_for_sink;

        /* Propagate down the filter tree */
        if (i->origin_sink) {
            size_t filter_result, left_to_play_origin;

            /* The recursive call works in the origin sink domain ... */
            left_to_play_origin = pa_convert_size(left_to_play, &i->sink->sample_spec, &i->origin_sink->sample_spec);

            /* .. and returns the time to sleep before waking up. We need the
             * underrun duration for comparisons, so we undo the subtraction on
             * the return value... */
            filter_result = left_to_play_origin - pa_sink_process_input_underruns(i->origin_sink, left_to_play_origin);

            /* ... and convert it back to the master sink domain */
            filter_result = pa_convert_size(filter_result, &i->origin_sink->sample_spec, &i->sink->sample_spec);

            /* Remember the longest underrun so far */
            if (filter_result > result)
                result = filter_result;
        }

        if (uf == 0) {
            /* No underrun here, move on */
            continue;
        } else if (uf >= left_to_play) {
            /* The sink has possibly consumed all the data the sink input provided */
            pa_sink_input_process_underrun(i);
        } else if (uf > result) {
            /* Remember the longest underrun so far */
            result = uf;
        }
    }

    if (result > 0)
        pa_log_debug("%s: Found underrun %ld bytes ago (%ld bytes ahead in playback buffer)", s->name,
                (long) result, (long) left_to_play - result);
    return left_to_play - result;
}

/* Called from IO thread context */
void pa_sink_process_rewind(pa_sink *s, size_t nbytes) {
    pa_sink_input *i;
    void *state = NULL;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(PA_SINK_IS_LINKED(s->thread_info.state));

    /* If nobody requested this and this is actually no real rewind
     * then we can short cut this. Please note that this means that
     * not all rewind requests triggered upstream will always be
     * translated in actual requests! */
    if (!s->thread_info.rewind_requested && nbytes <= 0)
        return;

    s->thread_info.rewind_nbytes = 0;
    s->thread_info.rewind_requested = false;

    if (nbytes > 0) {
        pa_log_debug("Processing rewind...");
        if (s->flags & PA_SINK_DEFERRED_VOLUME)
            pa_sink_volume_change_rewind(s, nbytes);
    }

    PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state) {
        pa_sink_input_assert_ref(i);
        pa_sink_input_process_rewind(i, nbytes);
    }

    if (nbytes > 0) {
        if (s->monitor_source && PA_SOURCE_IS_LINKED(s->monitor_source->thread_info.state))
            pa_source_process_rewind(s->monitor_source, nbytes);
    }
}

/* Called from IO thread context */
static unsigned fill_mix_info(pa_sink *s, size_t *length, pa_mix_info *info, unsigned maxinfo) {
    pa_sink_input *i;
    unsigned n = 0;
    void *state = NULL;
    size_t mixlength = *length;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(info);

    while ((i = pa_hashmap_iterate(s->thread_info.inputs, &state, NULL)) && maxinfo > 0) {
        pa_sink_input_assert_ref(i);

        pa_sink_input_peek(i, *length, &info->chunk, &info->volume);

        if (mixlength == 0 || info->chunk.length < mixlength)
            mixlength = info->chunk.length;

        if (pa_memblock_is_silence(info->chunk.memblock)) {
            pa_memblock_unref(info->chunk.memblock);
            continue;
        }

        info->userdata = pa_sink_input_ref(i);

        pa_assert(info->chunk.memblock);
        pa_assert(info->chunk.length > 0);

        info++;
        n++;
        maxinfo--;
    }

    if (mixlength > 0)
        *length = mixlength;

    return n;
}

/* Called from IO thread context */
static void inputs_drop(pa_sink *s, pa_mix_info *info, unsigned n, pa_memchunk *result) {
    pa_sink_input *i;
    void *state;
    unsigned p = 0;
    unsigned n_unreffed = 0;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(result);
    pa_assert(result->memblock);
    pa_assert(result->length > 0);

    /* We optimize for the case where the order of the inputs has not changed */

    PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state) {
        unsigned j;
        pa_mix_info* m = NULL;

        pa_sink_input_assert_ref(i);

        /* Let's try to find the matching entry info the pa_mix_info array */
        for (j = 0; j < n; j ++) {

            if (info[p].userdata == i) {
                m = info + p;
                break;
            }

            p++;
            if (p >= n)
                p = 0;
        }

        /* Drop read data */
        pa_sink_input_drop(i, result->length);

        if (s->monitor_source && PA_SOURCE_IS_LINKED(s->monitor_source->thread_info.state)) {

            if (pa_hashmap_size(i->thread_info.direct_outputs) > 0) {
                void *ostate = NULL;
                pa_source_output *o;
                pa_memchunk c;

                if (m && m->chunk.memblock) {
                    c = m->chunk;
                    pa_memblock_ref(c.memblock);
                    pa_assert(result->length <= c.length);
                    c.length = result->length;

                    pa_memchunk_make_writable(&c, 0);
                    pa_volume_memchunk(&c, &s->sample_spec, &m->volume);
                } else {
                    c = s->silence;
                    pa_memblock_ref(c.memblock);
                    pa_assert(result->length <= c.length);
                    c.length = result->length;
                }

                while ((o = pa_hashmap_iterate(i->thread_info.direct_outputs, &ostate, NULL))) {
                    pa_source_output_assert_ref(o);
                    pa_assert(o->direct_on_input == i);
                    pa_source_post_direct(s->monitor_source, o, &c);
                }

                pa_memblock_unref(c.memblock);
            }
        }

        if (m) {
            if (m->chunk.memblock) {
                pa_memblock_unref(m->chunk.memblock);
                pa_memchunk_reset(&m->chunk);
            }

            pa_sink_input_unref(m->userdata);
            m->userdata = NULL;

            n_unreffed += 1;
        }
    }

    /* Now drop references to entries that are included in the
     * pa_mix_info array but don't exist anymore */

    if (n_unreffed < n) {
        for (; n > 0; info++, n--) {
            if (info->userdata)
                pa_sink_input_unref(info->userdata);
            if (info->chunk.memblock)
                pa_memblock_unref(info->chunk.memblock);
        }
    }

    if (s->monitor_source && PA_SOURCE_IS_LINKED(s->monitor_source->thread_info.state))
        pa_source_post(s->monitor_source, result);
}

/* Called from IO thread context */
void pa_sink_render(pa_sink*s, size_t length, pa_memchunk *result) {
    pa_mix_info info[MAX_MIX_CHANNELS];
    unsigned n;
    size_t block_size_max;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(PA_SINK_IS_LINKED(s->thread_info.state));
    pa_assert(pa_frame_aligned(length, &s->sample_spec));
    pa_assert(result);

    pa_assert(!s->thread_info.rewind_requested);
    pa_assert(s->thread_info.rewind_nbytes == 0);

    if (s->thread_info.state == PA_SINK_SUSPENDED) {
        result->memblock = pa_memblock_ref(s->silence.memblock);
        result->index = s->silence.index;
        result->length = PA_MIN(s->silence.length, length);
        return;
    }

    pa_sink_ref(s);

    if (length <= 0)
        length = pa_frame_align(MIX_BUFFER_LENGTH, &s->sample_spec);

    block_size_max = pa_mempool_block_size_max(s->core->mempool);
    if (length > block_size_max)
        length = pa_frame_align(block_size_max, &s->sample_spec);

    pa_assert(length > 0);

    n = fill_mix_info(s, &length, info, MAX_MIX_CHANNELS);

    if (n == 0) {

        *result = s->silence;
        pa_memblock_ref(result->memblock);

        if (result->length > length)
            result->length = length;

    } else if (n == 1) {
        pa_cvolume volume;

        *result = info[0].chunk;
        pa_memblock_ref(result->memblock);

        if (result->length > length)
            result->length = length;

        pa_sw_cvolume_multiply(&volume, &s->thread_info.soft_volume, &info[0].volume);

        if (s->thread_info.soft_muted || pa_cvolume_is_muted(&volume)) {
            pa_memblock_unref(result->memblock);
            pa_silence_memchunk_get(&s->core->silence_cache,
                                    s->core->mempool,
                                    result,
                                    &s->sample_spec,
                                    result->length);
        } else if (!pa_cvolume_is_norm(&volume)) {
            pa_memchunk_make_writable(result, 0);
            pa_volume_memchunk(result, &s->sample_spec, &volume);
        }
    } else {
        void *ptr;
        result->memblock = pa_memblock_new(s->core->mempool, length);

        ptr = pa_memblock_acquire(result->memblock);
        result->length = pa_mix(info, n,
                                ptr, length,
                                &s->sample_spec,
                                &s->thread_info.soft_volume,
                                s->thread_info.soft_muted);
        pa_memblock_release(result->memblock);

        result->index = 0;
    }

    inputs_drop(s, info, n, result);

    pa_sink_unref(s);
}

/* Called from IO thread context */
void pa_sink_render_into(pa_sink*s, pa_memchunk *target) {
    pa_mix_info info[MAX_MIX_CHANNELS];
    unsigned n;
    size_t length, block_size_max;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(PA_SINK_IS_LINKED(s->thread_info.state));
    pa_assert(target);
    pa_assert(target->memblock);
    pa_assert(target->length > 0);
    pa_assert(pa_frame_aligned(target->length, &s->sample_spec));

    pa_assert(!s->thread_info.rewind_requested);
    pa_assert(s->thread_info.rewind_nbytes == 0);

    if (s->thread_info.state == PA_SINK_SUSPENDED) {
        pa_silence_memchunk(target, &s->sample_spec);
        return;
    }

    pa_sink_ref(s);

    length = target->length;
    block_size_max = pa_mempool_block_size_max(s->core->mempool);
    if (length > block_size_max)
        length = pa_frame_align(block_size_max, &s->sample_spec);

    pa_assert(length > 0);

    n = fill_mix_info(s, &length, info, MAX_MIX_CHANNELS);

    if (n == 0) {
        if (target->length > length)
            target->length = length;

        pa_silence_memchunk(target, &s->sample_spec);
    } else if (n == 1) {
        pa_cvolume volume;

        if (target->length > length)
            target->length = length;

        pa_sw_cvolume_multiply(&volume, &s->thread_info.soft_volume, &info[0].volume);

        if (s->thread_info.soft_muted || pa_cvolume_is_muted(&volume))
            pa_silence_memchunk(target, &s->sample_spec);
        else {
            pa_memchunk vchunk;

            vchunk = info[0].chunk;
            pa_memblock_ref(vchunk.memblock);

            if (vchunk.length > length)
                vchunk.length = length;

            if (!pa_cvolume_is_norm(&volume)) {
                pa_memchunk_make_writable(&vchunk, 0);
                pa_volume_memchunk(&vchunk, &s->sample_spec, &volume);
            }

            pa_memchunk_memcpy(target, &vchunk);
            pa_memblock_unref(vchunk.memblock);
        }

    } else {
        void *ptr;

        ptr = pa_memblock_acquire(target->memblock);

        target->length = pa_mix(info, n,
                                (uint8_t*) ptr + target->index, length,
                                &s->sample_spec,
                                &s->thread_info.soft_volume,
                                s->thread_info.soft_muted);

        pa_memblock_release(target->memblock);
    }

    inputs_drop(s, info, n, target);

    pa_sink_unref(s);
}

/* Called from IO thread context */
void pa_sink_render_into_full(pa_sink *s, pa_memchunk *target) {
    pa_memchunk chunk;
    size_t l, d;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(PA_SINK_IS_LINKED(s->thread_info.state));
    pa_assert(target);
    pa_assert(target->memblock);
    pa_assert(target->length > 0);
    pa_assert(pa_frame_aligned(target->length, &s->sample_spec));

    pa_assert(!s->thread_info.rewind_requested);
    pa_assert(s->thread_info.rewind_nbytes == 0);

    if (s->thread_info.state == PA_SINK_SUSPENDED) {
        pa_silence_memchunk(target, &s->sample_spec);
        return;
    }

    pa_sink_ref(s);

    l = target->length;
    d = 0;
    while (l > 0) {
        chunk = *target;
        chunk.index += d;
        chunk.length -= d;

        pa_sink_render_into(s, &chunk);

        d += chunk.length;
        l -= chunk.length;
    }

    pa_sink_unref(s);
}

/* Called from IO thread context */
void pa_sink_render_full(pa_sink *s, size_t length, pa_memchunk *result) {
    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(PA_SINK_IS_LINKED(s->thread_info.state));
    pa_assert(length > 0);
    pa_assert(pa_frame_aligned(length, &s->sample_spec));
    pa_assert(result);

    pa_assert(!s->thread_info.rewind_requested);
    pa_assert(s->thread_info.rewind_nbytes == 0);

    pa_sink_ref(s);

    pa_sink_render(s, length, result);

    if (result->length < length) {
        pa_memchunk chunk;

        pa_memchunk_make_writable(result, length);

        chunk.memblock = result->memblock;
        chunk.index = result->index + result->length;
        chunk.length = length - result->length;

        pa_sink_render_into_full(s, &chunk);

        result->length = length;
    }

    pa_sink_unref(s);
}

/* Called from main thread */
void pa_sink_reconfigure(pa_sink *s, pa_sample_spec *spec, bool passthrough) {
    pa_sample_spec desired_spec;
    uint32_t default_rate = s->default_sample_rate;
    uint32_t alternate_rate = s->alternate_sample_rate;
    uint32_t idx;
    pa_sink_input *i;
    bool default_rate_is_usable = false;
    bool alternate_rate_is_usable = false;
    bool avoid_resampling = s->avoid_resampling;

    if (pa_sample_spec_equal(spec, &s->sample_spec))
        return;

    if (!s->reconfigure)
        return;

    if (PA_UNLIKELY(default_rate == alternate_rate && !passthrough && !avoid_resampling)) {
        pa_log_debug("Default and alternate sample rates are the same, so there is no point in switching.");
        return;
    }

    if (PA_SINK_IS_RUNNING(s->state)) {
        pa_log_info("Cannot update sample spec, SINK_IS_RUNNING, will keep using %s and %u Hz",
                    pa_sample_format_to_string(s->sample_spec.format), s->sample_spec.rate);
        return;
    }

    if (s->monitor_source) {
        if (PA_SOURCE_IS_RUNNING(s->monitor_source->state) == true) {
            pa_log_info("Cannot update sample spec, monitor source is RUNNING");
            return;
        }
    }

    if (PA_UNLIKELY(!pa_sample_spec_valid(spec)))
        return;

    desired_spec = s->sample_spec;

    if (passthrough) {
        /* We have to try to use the sink input format and rate */
        desired_spec.format = spec->format;
        desired_spec.rate = spec->rate;

    } else if (avoid_resampling) {
        /* We just try to set the sink input's sample rate if it's not too low */
        if (spec->rate >= default_rate || spec->rate >= alternate_rate)
            desired_spec.rate = spec->rate;
        desired_spec.format = spec->format;

    } else if (default_rate == spec->rate || alternate_rate == spec->rate) {
        /* We can directly try to use this rate */
        desired_spec.rate = spec->rate;

    }

    if (desired_spec.rate != spec->rate) {
        /* See if we can pick a rate that results in less resampling effort */
        if (default_rate % 11025 == 0 && spec->rate % 11025 == 0)
            default_rate_is_usable = true;
        if (default_rate % 4000 == 0 && spec->rate % 4000 == 0)
            default_rate_is_usable = true;
        if (alternate_rate % 11025 == 0 && spec->rate % 11025 == 0)
            alternate_rate_is_usable = true;
        if (alternate_rate % 4000 == 0 && spec->rate % 4000 == 0)
            alternate_rate_is_usable = true;

        if (alternate_rate_is_usable && !default_rate_is_usable)
            desired_spec.rate = alternate_rate;
        else
            desired_spec.rate = default_rate;
    }

    if (pa_sample_spec_equal(&desired_spec, &s->sample_spec) && passthrough == pa_sink_is_passthrough(s))
        return;

    if (!passthrough && pa_sink_used_by(s) > 0)
        return;

    pa_log_debug("Suspending sink %s due to changing format, desired format = %s rate = %u",
                 s->name, pa_sample_format_to_string(desired_spec.format), desired_spec.rate);
    pa_sink_suspend(s, true, PA_SUSPEND_INTERNAL);

    s->reconfigure(s, &desired_spec, passthrough);

    /* update monitor source as well */
    if (s->monitor_source && !passthrough)
        pa_source_reconfigure(s->monitor_source, &s->sample_spec, false);
    pa_log_info("Reconfigured successfully");

    PA_IDXSET_FOREACH(i, s->inputs, idx) {
        if (i->state == PA_SINK_INPUT_CORKED)
            pa_sink_input_update_resampler(i);
    }

    pa_sink_suspend(s, false, PA_SUSPEND_INTERNAL);
}

/* Called from main thread */
pa_usec_t pa_sink_get_latency(pa_sink *s) {
    int64_t usec = 0;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    /* The returned value is supposed to be in the time domain of the sound card! */

    if (s->state == PA_SINK_SUSPENDED)
        return 0;

    if (!(s->flags & PA_SINK_LATENCY))
        return 0;

    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_GET_LATENCY, &usec, 0, NULL) == 0);

    /* the return value is unsigned, so check that the offset can be added to usec without
     * underflowing. */
    if (-s->port_latency_offset <= usec)
        usec += s->port_latency_offset;
    else
        usec = 0;

    return (pa_usec_t)usec;
}

/* Called from IO thread */
int64_t pa_sink_get_latency_within_thread(pa_sink *s, bool allow_negative) {
    int64_t usec = 0;
    pa_msgobject *o;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(PA_SINK_IS_LINKED(s->thread_info.state));

    /* The returned value is supposed to be in the time domain of the sound card! */

    if (s->thread_info.state == PA_SINK_SUSPENDED)
        return 0;

    if (!(s->flags & PA_SINK_LATENCY))
        return 0;

    o = PA_MSGOBJECT(s);

    /* FIXME: We probably should make this a proper vtable callback instead of going through process_msg() */

    o->process_msg(o, PA_SINK_MESSAGE_GET_LATENCY, &usec, 0, NULL);

    /* If allow_negative is false, the call should only return positive values, */
    usec += s->thread_info.port_latency_offset;
    if (!allow_negative && usec < 0)
        usec = 0;

    return usec;
}

/* Called from the main thread (and also from the IO thread while the main
 * thread is waiting).
 *
 * When a sink uses volume sharing, it never has the PA_SINK_FLAT_VOLUME flag
 * set. Instead, flat volume mode is detected by checking whether the root sink
 * has the flag set. */
bool pa_sink_flat_volume_enabled(pa_sink *s) {
    pa_sink_assert_ref(s);

    s = pa_sink_get_master(s);

    if (PA_LIKELY(s))
        return (s->flags & PA_SINK_FLAT_VOLUME);
    else
        return false;
}

/* Called from the main thread (and also from the IO thread while the main
 * thread is waiting). */
pa_sink *pa_sink_get_master(pa_sink *s) {
    pa_sink_assert_ref(s);

    while (s && (s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)) {
        if (PA_UNLIKELY(!s->input_to_master))
            return NULL;

        s = s->input_to_master->sink;
    }

    return s;
}

/* Called from main context */
bool pa_sink_is_filter(pa_sink *s) {
    pa_sink_assert_ref(s);

    return (s->input_to_master != NULL);
}

/* Called from main context */
bool pa_sink_is_passthrough(pa_sink *s) {
    pa_sink_input *alt_i;
    uint32_t idx;

    pa_sink_assert_ref(s);

    /* one and only one PASSTHROUGH input can possibly be connected */
    if (pa_idxset_size(s->inputs) == 1) {
        alt_i = pa_idxset_first(s->inputs, &idx);

        if (pa_sink_input_is_passthrough(alt_i))
            return true;
    }

    return false;
}

/* Called from main context */
void pa_sink_enter_passthrough(pa_sink *s) {
    pa_cvolume volume;

    /* The sink implementation is reconfigured for passthrough in
     * pa_sink_reconfigure(). This function sets the PA core objects to
     * passthrough mode. */

    /* disable the monitor in passthrough mode */
    if (s->monitor_source) {
        pa_log_debug("Suspending monitor source %s, because the sink is entering the passthrough mode.", s->monitor_source->name);
        pa_source_suspend(s->monitor_source, true, PA_SUSPEND_PASSTHROUGH);
    }

    /* set the volume to NORM */
    s->saved_volume = *pa_sink_get_volume(s, true);
    s->saved_save_volume = s->save_volume;

    pa_cvolume_set(&volume, s->sample_spec.channels, PA_MIN(s->base_volume, PA_VOLUME_NORM));
    pa_sink_set_volume(s, &volume, true, false);

    pa_log_debug("Suspending/Restarting sink %s to enter passthrough mode", s->name);
}

/* Called from main context */
void pa_sink_leave_passthrough(pa_sink *s) {
    /* Unsuspend monitor */
    if (s->monitor_source) {
        pa_log_debug("Resuming monitor source %s, because the sink is leaving the passthrough mode.", s->monitor_source->name);
        pa_source_suspend(s->monitor_source, false, PA_SUSPEND_PASSTHROUGH);
    }

    /* Restore sink volume to what it was before we entered passthrough mode */
    pa_sink_set_volume(s, &s->saved_volume, true, s->saved_save_volume);

    pa_cvolume_init(&s->saved_volume);
    s->saved_save_volume = false;

}

/* Called from main context. */
static void compute_reference_ratio(pa_sink_input *i) {
    unsigned c = 0;
    pa_cvolume remapped;
    pa_cvolume ratio;

    pa_assert(i);
    pa_assert(pa_sink_flat_volume_enabled(i->sink));

    /*
     * Calculates the reference ratio from the sink's reference
     * volume. This basically calculates:
     *
     * i->reference_ratio = i->volume / i->sink->reference_volume
     */

    remapped = i->sink->reference_volume;
    pa_cvolume_remap(&remapped, &i->sink->channel_map, &i->channel_map);

    ratio = i->reference_ratio;

    for (c = 0; c < i->sample_spec.channels; c++) {

        /* We don't update when the sink volume is 0 anyway */
        if (remapped.values[c] <= PA_VOLUME_MUTED)
            continue;

        /* Don't update the reference ratio unless necessary */
        if (pa_sw_volume_multiply(
                    ratio.values[c],
                    remapped.values[c]) == i->volume.values[c])
            continue;

        ratio.values[c] = pa_sw_volume_divide(
                i->volume.values[c],
                remapped.values[c]);
    }

    pa_sink_input_set_reference_ratio(i, &ratio);
}

/* Called from main context. Only called for the root sink in volume sharing
 * cases, except for internal recursive calls. */
static void compute_reference_ratios(pa_sink *s) {
    uint32_t idx;
    pa_sink_input *i;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));
    pa_assert(pa_sink_flat_volume_enabled(s));

    PA_IDXSET_FOREACH(i, s->inputs, idx) {
        compute_reference_ratio(i);

        if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)
                && PA_SINK_IS_LINKED(i->origin_sink->state))
            compute_reference_ratios(i->origin_sink);
    }
}

/* Called from main context. Only called for the root sink in volume sharing
 * cases, except for internal recursive calls. */
static void compute_real_ratios(pa_sink *s) {
    pa_sink_input *i;
    uint32_t idx;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));
    pa_assert(pa_sink_flat_volume_enabled(s));

    PA_IDXSET_FOREACH(i, s->inputs, idx) {
        unsigned c;
        pa_cvolume remapped;

        if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)) {
            /* The origin sink uses volume sharing, so this input's real ratio
             * is handled as a special case - the real ratio must be 0 dB, and
             * as a result i->soft_volume must equal i->volume_factor. */
            pa_cvolume_reset(&i->real_ratio, i->real_ratio.channels);
            i->soft_volume = i->volume_factor;

            if (PA_SINK_IS_LINKED(i->origin_sink->state))
                compute_real_ratios(i->origin_sink);

            continue;
        }

        /*
         * This basically calculates:
         *
         * i->real_ratio := i->volume / s->real_volume
         * i->soft_volume := i->real_ratio * i->volume_factor
         */

        remapped = s->real_volume;
        pa_cvolume_remap(&remapped, &s->channel_map, &i->channel_map);

        i->real_ratio.channels = i->sample_spec.channels;
        i->soft_volume.channels = i->sample_spec.channels;

        for (c = 0; c < i->sample_spec.channels; c++) {

            if (remapped.values[c] <= PA_VOLUME_MUTED) {
                /* We leave i->real_ratio untouched */
                i->soft_volume.values[c] = PA_VOLUME_MUTED;
                continue;
            }

            /* Don't lose accuracy unless necessary */
            if (pa_sw_volume_multiply(
                        i->real_ratio.values[c],
                        remapped.values[c]) != i->volume.values[c])

                i->real_ratio.values[c] = pa_sw_volume_divide(
                        i->volume.values[c],
                        remapped.values[c]);

            i->soft_volume.values[c] = pa_sw_volume_multiply(
                    i->real_ratio.values[c],
                    i->volume_factor.values[c]);
        }

        /* We don't copy the soft_volume to the thread_info data
         * here. That must be done by the caller */
    }
}

static pa_cvolume *cvolume_remap_minimal_impact(
        pa_cvolume *v,
        const pa_cvolume *template,
        const pa_channel_map *from,
        const pa_channel_map *to) {

    pa_cvolume t;

    pa_assert(v);
    pa_assert(template);
    pa_assert(from);
    pa_assert(to);
    pa_assert(pa_cvolume_compatible_with_channel_map(v, from));
    pa_assert(pa_cvolume_compatible_with_channel_map(template, to));

    /* Much like pa_cvolume_remap(), but tries to minimize impact when
     * mapping from sink input to sink volumes:
     *
     * If template is a possible remapping from v it is used instead
     * of remapping anew.
     *
     * If the channel maps don't match we set an all-channel volume on
     * the sink to ensure that changing a volume on one stream has no
     * effect that cannot be compensated for in another stream that
     * does not have the same channel map as the sink. */

    if (pa_channel_map_equal(from, to))
        return v;

    t = *template;
    if (pa_cvolume_equal(pa_cvolume_remap(&t, to, from), v)) {
        *v = *template;
        return v;
    }

    pa_cvolume_set(v, to->channels, pa_cvolume_max(v));
    return v;
}

/* Called from main thread. Only called for the root sink in volume sharing
 * cases, except for internal recursive calls. */
static void get_maximum_input_volume(pa_sink *s, pa_cvolume *max_volume, const pa_channel_map *channel_map) {
    pa_sink_input *i;
    uint32_t idx;

    pa_sink_assert_ref(s);
    pa_assert(max_volume);
    pa_assert(channel_map);
    pa_assert(pa_sink_flat_volume_enabled(s));

    PA_IDXSET_FOREACH(i, s->inputs, idx) {
        pa_cvolume remapped;

        if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)) {
            if (PA_SINK_IS_LINKED(i->origin_sink->state))
                get_maximum_input_volume(i->origin_sink, max_volume, channel_map);

            /* Ignore this input. The origin sink uses volume sharing, so this
             * input's volume will be set to be equal to the root sink's real
             * volume. Obviously this input's current volume must not then
             * affect what the root sink's real volume will be. */
            continue;
        }

        remapped = i->volume;
        cvolume_remap_minimal_impact(&remapped, max_volume, &i->channel_map, channel_map);
        pa_cvolume_merge(max_volume, max_volume, &remapped);
    }
}

/* Called from main thread. Only called for the root sink in volume sharing
 * cases, except for internal recursive calls. */
static bool has_inputs(pa_sink *s) {
    pa_sink_input *i;
    uint32_t idx;

    pa_sink_assert_ref(s);

    PA_IDXSET_FOREACH(i, s->inputs, idx) {
        if (!i->origin_sink || !(i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER) || has_inputs(i->origin_sink))
            return true;
    }

    return false;
}

/* Called from main thread. Only called for the root sink in volume sharing
 * cases, except for internal recursive calls. */
static void update_real_volume(pa_sink *s, const pa_cvolume *new_volume, pa_channel_map *channel_map) {
    pa_sink_input *i;
    uint32_t idx;

    pa_sink_assert_ref(s);
    pa_assert(new_volume);
    pa_assert(channel_map);

    s->real_volume = *new_volume;
    pa_cvolume_remap(&s->real_volume, channel_map, &s->channel_map);

    PA_IDXSET_FOREACH(i, s->inputs, idx) {
        if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)) {
            if (pa_sink_flat_volume_enabled(s)) {
                pa_cvolume new_input_volume;

                /* Follow the root sink's real volume. */
                new_input_volume = *new_volume;
                pa_cvolume_remap(&new_input_volume, channel_map, &i->channel_map);
                pa_sink_input_set_volume_direct(i, &new_input_volume);
                compute_reference_ratio(i);
            }

            if (PA_SINK_IS_LINKED(i->origin_sink->state))
                update_real_volume(i->origin_sink, new_volume, channel_map);
        }
    }
}

/* Called from main thread. Only called for the root sink in shared volume
 * cases. */
static void compute_real_volume(pa_sink *s) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));
    pa_assert(pa_sink_flat_volume_enabled(s));
    pa_assert(!(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER));

    /* This determines the maximum volume of all streams and sets
     * s->real_volume accordingly. */

    if (!has_inputs(s)) {
        /* In the special case that we have no sink inputs we leave the
         * volume unmodified. */
        update_real_volume(s, &s->reference_volume, &s->channel_map);
        return;
    }

    pa_cvolume_mute(&s->real_volume, s->channel_map.channels);

    /* First let's determine the new maximum volume of all inputs
     * connected to this sink */
    get_maximum_input_volume(s, &s->real_volume, &s->channel_map);
    update_real_volume(s, &s->real_volume, &s->channel_map);

    /* Then, let's update the real ratios/soft volumes of all inputs
     * connected to this sink */
    compute_real_ratios(s);
}

/* Called from main thread. Only called for the root sink in shared volume
 * cases, except for internal recursive calls. */
static void propagate_reference_volume(pa_sink *s) {
    pa_sink_input *i;
    uint32_t idx;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));
    pa_assert(pa_sink_flat_volume_enabled(s));

    /* This is called whenever the sink volume changes that is not
     * caused by a sink input volume change. We need to fix up the
     * sink input volumes accordingly */

    PA_IDXSET_FOREACH(i, s->inputs, idx) {
        pa_cvolume new_volume;

        if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)) {
            if (PA_SINK_IS_LINKED(i->origin_sink->state))
                propagate_reference_volume(i->origin_sink);

            /* Since the origin sink uses volume sharing, this input's volume
             * needs to be updated to match the root sink's real volume, but
             * that will be done later in update_real_volume(). */
            continue;
        }

        /* This basically calculates:
         *
         * i->volume := s->reference_volume * i->reference_ratio  */

        new_volume = s->reference_volume;
        pa_cvolume_remap(&new_volume, &s->channel_map, &i->channel_map);
        pa_sw_cvolume_multiply(&new_volume, &new_volume, &i->reference_ratio);
        pa_sink_input_set_volume_direct(i, &new_volume);
    }
}

/* Called from main thread. Only called for the root sink in volume sharing
 * cases, except for internal recursive calls. The return value indicates
 * whether any reference volume actually changed. */
static bool update_reference_volume(pa_sink *s, const pa_cvolume *v, const pa_channel_map *channel_map, bool save) {
    pa_cvolume volume;
    bool reference_volume_changed;
    pa_sink_input *i;
    uint32_t idx;

    pa_sink_assert_ref(s);
    pa_assert(PA_SINK_IS_LINKED(s->state));
    pa_assert(v);
    pa_assert(channel_map);
    pa_assert(pa_cvolume_valid(v));

    volume = *v;
    pa_cvolume_remap(&volume, channel_map, &s->channel_map);

    reference_volume_changed = !pa_cvolume_equal(&volume, &s->reference_volume);
    pa_sink_set_reference_volume_direct(s, &volume);

    s->save_volume = (!reference_volume_changed && s->save_volume) || save;

    if (!reference_volume_changed && !(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER))
        /* If the root sink's volume doesn't change, then there can't be any
         * changes in the other sinks in the sink tree either.
         *
         * It's probably theoretically possible that even if the root sink's
         * volume changes slightly, some filter sink doesn't change its volume
         * due to rounding errors. If that happens, we still want to propagate
         * the changed root sink volume to the sinks connected to the
         * intermediate sink that didn't change its volume. This theoretical
         * possibility is the reason why we have that !(s->flags &
         * PA_SINK_SHARE_VOLUME_WITH_MASTER) condition. Probably nobody would
         * notice even if we returned here false always if
         * reference_volume_changed is false. */
        return false;

    PA_IDXSET_FOREACH(i, s->inputs, idx) {
        if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)
                && PA_SINK_IS_LINKED(i->origin_sink->state))
            update_reference_volume(i->origin_sink, v, channel_map, false);
    }

    return true;
}

/* Called from main thread */
void pa_sink_set_volume(
        pa_sink *s,
        const pa_cvolume *volume,
        bool send_msg,
        bool save) {

    pa_cvolume new_reference_volume;
    pa_sink *root_sink;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));
    pa_assert(!volume || pa_cvolume_valid(volume));
    pa_assert(volume || pa_sink_flat_volume_enabled(s));
    pa_assert(!volume || volume->channels == 1 || pa_cvolume_compatible(volume, &s->sample_spec));

    /* make sure we don't change the volume when a PASSTHROUGH input is connected ...
     * ... *except* if we're being invoked to reset the volume to ensure 0 dB gain */
    if (pa_sink_is_passthrough(s) && (!volume || !pa_cvolume_is_norm(volume))) {
        pa_log_warn("Cannot change volume, Sink is connected to PASSTHROUGH input");
        return;
    }

    /* In case of volume sharing, the volume is set for the root sink first,
     * from which it's then propagated to the sharing sinks. */
    root_sink = pa_sink_get_master(s);

    if (PA_UNLIKELY(!root_sink))
        return;

    /* As a special exception we accept mono volumes on all sinks --
     * even on those with more complex channel maps */

    if (volume) {
        if (pa_cvolume_compatible(volume, &s->sample_spec))
            new_reference_volume = *volume;
        else {
            new_reference_volume = s->reference_volume;
            pa_cvolume_scale(&new_reference_volume, pa_cvolume_max(volume));
        }

        pa_cvolume_remap(&new_reference_volume, &s->channel_map, &root_sink->channel_map);

        if (update_reference_volume(root_sink, &new_reference_volume, &root_sink->channel_map, save)) {
            if (pa_sink_flat_volume_enabled(root_sink)) {
                /* OK, propagate this volume change back to the inputs */
                propagate_reference_volume(root_sink);

                /* And now recalculate the real volume */
                compute_real_volume(root_sink);
            } else
                update_real_volume(root_sink, &root_sink->reference_volume, &root_sink->channel_map);
        }

    } else {
        /* If volume is NULL we synchronize the sink's real and
         * reference volumes with the stream volumes. */

        pa_assert(pa_sink_flat_volume_enabled(root_sink));

        /* Ok, let's determine the new real volume */
        compute_real_volume(root_sink);

        /* Let's 'push' the reference volume if necessary */
        pa_cvolume_merge(&new_reference_volume, &s->reference_volume, &root_sink->real_volume);
        /* If the sink and its root don't have the same number of channels, we need to remap */
        if (s != root_sink && !pa_channel_map_equal(&s->channel_map, &root_sink->channel_map))
            pa_cvolume_remap(&new_reference_volume, &s->channel_map, &root_sink->channel_map);
        update_reference_volume(root_sink, &new_reference_volume, &root_sink->channel_map, save);

        /* Now that the reference volume is updated, we can update the streams'
         * reference ratios. */
        compute_reference_ratios(root_sink);
    }

    if (root_sink->set_volume) {
        /* If we have a function set_volume(), then we do not apply a
         * soft volume by default. However, set_volume() is free to
         * apply one to root_sink->soft_volume */

        pa_cvolume_reset(&root_sink->soft_volume, root_sink->sample_spec.channels);
        if (!(root_sink->flags & PA_SINK_DEFERRED_VOLUME))
            root_sink->set_volume(root_sink);

    } else
        /* If we have no function set_volume(), then the soft volume
         * becomes the real volume */
        root_sink->soft_volume = root_sink->real_volume;

    /* This tells the sink that soft volume and/or real volume changed */
    if (send_msg)
        pa_assert_se(pa_asyncmsgq_send(root_sink->asyncmsgq, PA_MSGOBJECT(root_sink), PA_SINK_MESSAGE_SET_SHARED_VOLUME, NULL, 0, NULL) == 0);
}

/* Called from the io thread if sync volume is used, otherwise from the main thread.
 * Only to be called by sink implementor */
void pa_sink_set_soft_volume(pa_sink *s, const pa_cvolume *volume) {

    pa_sink_assert_ref(s);
    pa_assert(!(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER));

    if (s->flags & PA_SINK_DEFERRED_VOLUME)
        pa_sink_assert_io_context(s);
    else
        pa_assert_ctl_context();

    if (!volume)
        pa_cvolume_reset(&s->soft_volume, s->sample_spec.channels);
    else
        s->soft_volume = *volume;

    if (PA_SINK_IS_LINKED(s->state) && !(s->flags & PA_SINK_DEFERRED_VOLUME))
        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_SET_VOLUME, NULL, 0, NULL) == 0);
    else
        s->thread_info.soft_volume = s->soft_volume;
}

/* Called from the main thread. Only called for the root sink in volume sharing
 * cases, except for internal recursive calls. */
static void propagate_real_volume(pa_sink *s, const pa_cvolume *old_real_volume) {
    pa_sink_input *i;
    uint32_t idx;

    pa_sink_assert_ref(s);
    pa_assert(old_real_volume);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    /* This is called when the hardware's real volume changes due to
     * some external event. We copy the real volume into our
     * reference volume and then rebuild the stream volumes based on
     * i->real_ratio which should stay fixed. */

    if (!(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)) {
        if (pa_cvolume_equal(old_real_volume, &s->real_volume))
            return;

        /* 1. Make the real volume the reference volume */
        update_reference_volume(s, &s->real_volume, &s->channel_map, true);
    }

    if (pa_sink_flat_volume_enabled(s)) {

        PA_IDXSET_FOREACH(i, s->inputs, idx) {
            pa_cvolume new_volume;

            /* 2. Since the sink's reference and real volumes are equal
             * now our ratios should be too. */
            pa_sink_input_set_reference_ratio(i, &i->real_ratio);

            /* 3. Recalculate the new stream reference volume based on the
             * reference ratio and the sink's reference volume.
             *
             * This basically calculates:
             *
             * i->volume = s->reference_volume * i->reference_ratio
             *
             * This is identical to propagate_reference_volume() */
            new_volume = s->reference_volume;
            pa_cvolume_remap(&new_volume, &s->channel_map, &i->channel_map);
            pa_sw_cvolume_multiply(&new_volume, &new_volume, &i->reference_ratio);
            pa_sink_input_set_volume_direct(i, &new_volume);

            if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)
                    && PA_SINK_IS_LINKED(i->origin_sink->state))
                propagate_real_volume(i->origin_sink, old_real_volume);
        }
    }

    /* Something got changed in the hardware. It probably makes sense
     * to save changed hw settings given that hw volume changes not
     * triggered by PA are almost certainly done by the user. */
    if (!(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER))
        s->save_volume = true;
}

/* Called from io thread */
void pa_sink_update_volume_and_mute(pa_sink *s) {
    pa_assert(s);
    pa_sink_assert_io_context(s);

    pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_UPDATE_VOLUME_AND_MUTE, NULL, 0, NULL, NULL);
}

/* Called from main thread */
const pa_cvolume *pa_sink_get_volume(pa_sink *s, bool force_refresh) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    if (s->refresh_volume || force_refresh) {
        struct pa_cvolume old_real_volume;

        pa_assert(!(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER));

        old_real_volume = s->real_volume;

        if (!(s->flags & PA_SINK_DEFERRED_VOLUME) && s->get_volume)
            s->get_volume(s);

        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_GET_VOLUME, NULL, 0, NULL) == 0);

        update_real_volume(s, &s->real_volume, &s->channel_map);
        propagate_real_volume(s, &old_real_volume);
    }

    return &s->reference_volume;
}

/* Called from main thread. In volume sharing cases, only the root sink may
 * call this. */
void pa_sink_volume_changed(pa_sink *s, const pa_cvolume *new_real_volume) {
    pa_cvolume old_real_volume;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));
    pa_assert(!(s->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER));

    /* The sink implementor may call this if the volume changed to make sure everyone is notified */

    old_real_volume = s->real_volume;
    update_real_volume(s, new_real_volume, &s->channel_map);
    propagate_real_volume(s, &old_real_volume);
}

/* Called from main thread */
void pa_sink_set_mute(pa_sink *s, bool mute, bool save) {
    bool old_muted;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    old_muted = s->muted;

    if (mute == old_muted) {
        s->save_muted |= save;
        return;
    }

    s->muted = mute;
    s->save_muted = save;

    if (!(s->flags & PA_SINK_DEFERRED_VOLUME) && s->set_mute) {
        s->set_mute_in_progress = true;
        s->set_mute(s);
        s->set_mute_in_progress = false;
    }

    if (!PA_SINK_IS_LINKED(s->state))
        return;

    pa_log_debug("The mute of sink %s changed from %s to %s.", s->name, pa_yes_no(old_muted), pa_yes_no(mute));
    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_SET_MUTE, NULL, 0, NULL) == 0);
    pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
    pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_MUTE_CHANGED], s);
}

/* Called from main thread */
bool pa_sink_get_mute(pa_sink *s, bool force_refresh) {

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    if ((s->refresh_muted || force_refresh) && s->get_mute) {
        bool mute;

        if (s->flags & PA_SINK_DEFERRED_VOLUME) {
            if (pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_GET_MUTE, &mute, 0, NULL) >= 0)
                pa_sink_mute_changed(s, mute);
        } else {
            if (s->get_mute(s, &mute) >= 0)
                pa_sink_mute_changed(s, mute);
        }
    }

    return s->muted;
}

/* Called from main thread */
void pa_sink_mute_changed(pa_sink *s, bool new_muted) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    if (s->set_mute_in_progress)
        return;

    /* pa_sink_set_mute() does this same check, so this may appear redundant,
     * but we must have this here also, because the save parameter of
     * pa_sink_set_mute() would otherwise have unintended side effects (saving
     * the mute state when it shouldn't be saved). */
    if (new_muted == s->muted)
        return;

    pa_sink_set_mute(s, new_muted, true);
}

/* Called from main thread */
bool pa_sink_update_proplist(pa_sink *s, pa_update_mode_t mode, pa_proplist *p) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    if (p)
        pa_proplist_update(s->proplist, mode, p);

    if (PA_SINK_IS_LINKED(s->state)) {
        pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_PROPLIST_CHANGED], s);
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
    }

    return true;
}

/* Called from main thread */
/* FIXME -- this should be dropped and be merged into pa_sink_update_proplist() */
void pa_sink_set_description(pa_sink *s, const char *description) {
    const char *old;
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    if (!description && !pa_proplist_contains(s->proplist, PA_PROP_DEVICE_DESCRIPTION))
        return;

    old = pa_proplist_gets(s->proplist, PA_PROP_DEVICE_DESCRIPTION);

    if (old && description && pa_streq(old, description))
        return;

    if (description)
        pa_proplist_sets(s->proplist, PA_PROP_DEVICE_DESCRIPTION, description);
    else
        pa_proplist_unset(s->proplist, PA_PROP_DEVICE_DESCRIPTION);

    if (s->monitor_source) {
        char *n;

        n = pa_sprintf_malloc("Monitor Source of %s", description ? description : s->name);
        pa_source_set_description(s->monitor_source, n);
        pa_xfree(n);
    }

    if (PA_SINK_IS_LINKED(s->state)) {
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
        pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_PROPLIST_CHANGED], s);
    }
}

/* Called from main thread */
unsigned pa_sink_linked_by(pa_sink *s) {
    unsigned ret;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    ret = pa_idxset_size(s->inputs);

    /* We add in the number of streams connected to us here. Please
     * note the asymmetry to pa_sink_used_by()! */

    if (s->monitor_source)
        ret += pa_source_linked_by(s->monitor_source);

    return ret;
}

/* Called from main thread */
unsigned pa_sink_used_by(pa_sink *s) {
    unsigned ret;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    ret = pa_idxset_size(s->inputs);
    pa_assert(ret >= s->n_corked);

    /* Streams connected to our monitor source do not matter for
     * pa_sink_used_by()!.*/

    return ret - s->n_corked;
}

/* Called from main thread */
unsigned pa_sink_check_suspend(pa_sink *s, pa_sink_input *ignore_input, pa_source_output *ignore_output) {
    unsigned ret;
    pa_sink_input *i;
    uint32_t idx;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    if (!PA_SINK_IS_LINKED(s->state))
        return 0;

    ret = 0;

    PA_IDXSET_FOREACH(i, s->inputs, idx) {
        if (i == ignore_input)
            continue;

        /* We do not assert here. It is perfectly valid for a sink input to
         * be in the INIT state (i.e. created, marked done but not yet put)
         * and we should not care if it's unlinked as it won't contribute
         * towards our busy status.
         */
        if (!PA_SINK_INPUT_IS_LINKED(i->state))
            continue;

        if (i->state == PA_SINK_INPUT_CORKED)
            continue;

        if (i->flags & PA_SINK_INPUT_DONT_INHIBIT_AUTO_SUSPEND)
            continue;

        ret ++;
    }

    if (s->monitor_source)
        ret += pa_source_check_suspend(s->monitor_source, ignore_output);

    return ret;
}

const char *pa_sink_state_to_string(pa_sink_state_t state) {
    switch (state) {
        case PA_SINK_INIT:          return "INIT";
        case PA_SINK_IDLE:          return "IDLE";
        case PA_SINK_RUNNING:       return "RUNNING";
        case PA_SINK_SUSPENDED:     return "SUSPENDED";
        case PA_SINK_UNLINKED:      return "UNLINKED";
        case PA_SINK_INVALID_STATE: return "INVALID_STATE";
    }

    pa_assert_not_reached();
}

/* Called from the IO thread */
static void sync_input_volumes_within_thread(pa_sink *s) {
    pa_sink_input *i;
    void *state = NULL;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);

    PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state) {
        if (pa_cvolume_equal(&i->thread_info.soft_volume, &i->soft_volume))
            continue;

        i->thread_info.soft_volume = i->soft_volume;
        pa_sink_input_request_rewind(i, 0, true, false, false);
    }
}

/* Called from the IO thread. Only called for the root sink in volume sharing
 * cases, except for internal recursive calls. */
static void set_shared_volume_within_thread(pa_sink *s) {
    pa_sink_input *i = NULL;
    void *state = NULL;

    pa_sink_assert_ref(s);

    PA_MSGOBJECT(s)->process_msg(PA_MSGOBJECT(s), PA_SINK_MESSAGE_SET_VOLUME_SYNCED, NULL, 0, NULL);

    PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state) {
        if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER))
            set_shared_volume_within_thread(i->origin_sink);
    }
}

/* Called from IO thread, except when it is not */
int pa_sink_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk) {
    pa_sink *s = PA_SINK(o);
    pa_sink_assert_ref(s);

    switch ((pa_sink_message_t) code) {

        case PA_SINK_MESSAGE_ADD_INPUT: {
            pa_sink_input *i = PA_SINK_INPUT(userdata);

            /* If you change anything here, make sure to change the
             * sink input handling a few lines down at
             * PA_SINK_MESSAGE_FINISH_MOVE, too. */

            pa_hashmap_put(s->thread_info.inputs, PA_UINT32_TO_PTR(i->index), pa_sink_input_ref(i));

            /* Since the caller sleeps in pa_sink_input_put(), we can
             * safely access data outside of thread_info even though
             * it is mutable */

            if ((i->thread_info.sync_prev = i->sync_prev)) {
                pa_assert(i->sink == i->thread_info.sync_prev->sink);
                pa_assert(i->sync_prev->sync_next == i);
                i->thread_info.sync_prev->thread_info.sync_next = i;
            }

            if ((i->thread_info.sync_next = i->sync_next)) {
                pa_assert(i->sink == i->thread_info.sync_next->sink);
                pa_assert(i->sync_next->sync_prev == i);
                i->thread_info.sync_next->thread_info.sync_prev = i;
            }

            pa_sink_input_attach(i);

            pa_sink_input_set_state_within_thread(i, i->state);

            /* The requested latency of the sink input needs to be fixed up and
             * then configured on the sink. If this causes the sink latency to
             * go down, the sink implementor is responsible for doing a rewind
             * in the update_requested_latency() callback to ensure that the
             * sink buffer doesn't contain more data than what the new latency
             * allows.
             *
             * XXX: Does it really make sense to push this responsibility to
             * the sink implementors? Wouldn't it be better to do it once in
             * the core than many times in the modules? */

            if (i->thread_info.requested_sink_latency != (pa_usec_t) -1)
                pa_sink_input_set_requested_latency_within_thread(i, i->thread_info.requested_sink_latency);

            pa_sink_input_update_max_rewind(i, s->thread_info.max_rewind);
            pa_sink_input_update_max_request(i, s->thread_info.max_request);

            /* We don't rewind here automatically. This is left to the
             * sink input implementor because some sink inputs need a
             * slow start, i.e. need some time to buffer client
             * samples before beginning streaming.
             *
             * XXX: Does it really make sense to push this functionality to
             * the sink implementors? Wouldn't it be better to do it once in
             * the core than many times in the modules? */

            /* In flat volume mode we need to update the volume as
             * well */
            return o->process_msg(o, PA_SINK_MESSAGE_SET_SHARED_VOLUME, NULL, 0, NULL);
        }

        case PA_SINK_MESSAGE_REMOVE_INPUT: {
            pa_sink_input *i = PA_SINK_INPUT(userdata);

            /* If you change anything here, make sure to change the
             * sink input handling a few lines down at
             * PA_SINK_MESSAGE_START_MOVE, too. */

            pa_sink_input_detach(i);

            pa_sink_input_set_state_within_thread(i, i->state);

            /* Since the caller sleeps in pa_sink_input_unlink(),
             * we can safely access data outside of thread_info even
             * though it is mutable */

            pa_assert(!i->sync_prev);
            pa_assert(!i->sync_next);

            if (i->thread_info.sync_prev) {
                i->thread_info.sync_prev->thread_info.sync_next = i->thread_info.sync_prev->sync_next;
                i->thread_info.sync_prev = NULL;
            }

            if (i->thread_info.sync_next) {
                i->thread_info.sync_next->thread_info.sync_prev = i->thread_info.sync_next->sync_prev;
                i->thread_info.sync_next = NULL;
            }

            pa_hashmap_remove_and_free(s->thread_info.inputs, PA_UINT32_TO_PTR(i->index));
            pa_sink_invalidate_requested_latency(s, true);
            pa_sink_request_rewind(s, (size_t) -1);

            /* In flat volume mode we need to update the volume as
             * well */
            return o->process_msg(o, PA_SINK_MESSAGE_SET_SHARED_VOLUME, NULL, 0, NULL);
        }

        case PA_SINK_MESSAGE_START_MOVE: {
            pa_sink_input *i = PA_SINK_INPUT(userdata);

            /* We don't support moving synchronized streams. */
            pa_assert(!i->sync_prev);
            pa_assert(!i->sync_next);
            pa_assert(!i->thread_info.sync_next);
            pa_assert(!i->thread_info.sync_prev);

            if (i->thread_info.state != PA_SINK_INPUT_CORKED) {
                pa_usec_t usec = 0;
                size_t sink_nbytes, total_nbytes;

                /* The old sink probably has some audio from this
                 * stream in its buffer. We want to "take it back" as
                 * much as possible and play it to the new sink. We
                 * don't know at this point how much the old sink can
                 * rewind. We have to pick something, and that
                 * something is the full latency of the old sink here.
                 * So we rewind the stream buffer by the sink latency
                 * amount, which may be more than what we should
                 * rewind. This can result in a chunk of audio being
                 * played both to the old sink and the new sink.
                 *
                 * FIXME: Fix this code so that we don't have to make
                 * guesses about how much the sink will actually be
                 * able to rewind. If someone comes up with a solution
                 * for this, something to note is that the part of the
                 * latency that the old sink couldn't rewind should
                 * ideally be compensated after the stream has moved
                 * to the new sink by adding silence. The new sink
                 * most likely can't start playing the moved stream
                 * immediately, and that gap should be removed from
                 * the "compensation silence" (at least at the time of
                 * writing this, the move finish code will actually
                 * already take care of dropping the new sink's
                 * unrewindable latency, so taking into account the
                 * unrewindable latency of the old sink is the only
                 * problem).
                 *
                 * The render_memblockq contents are discarded,
                 * because when the sink changes, the format of the
                 * audio stored in the render_memblockq may change
                 * too, making the stored audio invalid. FIXME:
                 * However, the read and write indices are moved back
                 * the same amount, so if they are not the same now,
                 * they won't be the same after the rewind either. If
                 * the write index of the render_memblockq is ahead of
                 * the read index, then the render_memblockq will feed
                 * the new sink some silence first, which it shouldn't
                 * do. The write index should be flushed to be the
                 * same as the read index. */

                /* Get the latency of the sink */
                usec = pa_sink_get_latency_within_thread(s, false);
                sink_nbytes = pa_usec_to_bytes(usec, &s->sample_spec);
                total_nbytes = sink_nbytes + pa_memblockq_get_length(i->thread_info.render_memblockq);

                if (total_nbytes > 0) {
                    i->thread_info.rewrite_nbytes = i->thread_info.resampler ? pa_resampler_request(i->thread_info.resampler, total_nbytes) : total_nbytes;
                    i->thread_info.rewrite_flush = true;
                    pa_sink_input_process_rewind(i, sink_nbytes);
                }
            }

            pa_sink_input_detach(i);

            /* Let's remove the sink input ...*/
            pa_hashmap_remove_and_free(s->thread_info.inputs, PA_UINT32_TO_PTR(i->index));

            pa_sink_invalidate_requested_latency(s, true);

            pa_log_debug("Requesting rewind due to started move");
            pa_sink_request_rewind(s, (size_t) -1);

            /* In flat volume mode we need to update the volume as
             * well */
            return o->process_msg(o, PA_SINK_MESSAGE_SET_SHARED_VOLUME, NULL, 0, NULL);
        }

        case PA_SINK_MESSAGE_FINISH_MOVE: {
            pa_sink_input *i = PA_SINK_INPUT(userdata);

            /* We don't support moving synchronized streams. */
            pa_assert(!i->sync_prev);
            pa_assert(!i->sync_next);
            pa_assert(!i->thread_info.sync_next);
            pa_assert(!i->thread_info.sync_prev);

            pa_hashmap_put(s->thread_info.inputs, PA_UINT32_TO_PTR(i->index), pa_sink_input_ref(i));

            pa_sink_input_attach(i);

            if (i->thread_info.state != PA_SINK_INPUT_CORKED) {
                pa_usec_t usec = 0;
                size_t nbytes;

                /* In the ideal case the new sink would start playing
                 * the stream immediately. That requires the sink to
                 * be able to rewind all of its latency, which usually
                 * isn't possible, so there will probably be some gap
                 * before the moved stream becomes audible. We then
                 * have two possibilities: 1) start playing the stream
                 * from where it is now, or 2) drop the unrewindable
                 * latency of the sink from the stream. With option 1
                 * we won't lose any audio but the stream will have a
                 * pause. With option 2 we may lose some audio but the
                 * stream time will be somewhat in sync with the wall
                 * clock. Lennart seems to have chosen option 2 (one
                 * of the reasons might have been that option 1 is
                 * actually much harder to implement), so we drop the
                 * latency of the new sink from the moved stream and
                 * hope that the sink will undo most of that in the
                 * rewind. */

                /* Get the latency of the sink */
                usec = pa_sink_get_latency_within_thread(s, false);
                nbytes = pa_usec_to_bytes(usec, &s->sample_spec);

                if (nbytes > 0)
                    pa_sink_input_drop(i, nbytes);

                pa_log_debug("Requesting rewind due to finished move");
                pa_sink_request_rewind(s, nbytes);
            }

            /* Updating the requested sink latency has to be done
             * after the sink rewind request, not before, because
             * otherwise the sink may limit the rewind amount
             * needlessly. */

            if (i->thread_info.requested_sink_latency != (pa_usec_t) -1)
                pa_sink_input_set_requested_latency_within_thread(i, i->thread_info.requested_sink_latency);

            pa_sink_input_update_max_rewind(i, s->thread_info.max_rewind);
            pa_sink_input_update_max_request(i, s->thread_info.max_request);

            return o->process_msg(o, PA_SINK_MESSAGE_SET_SHARED_VOLUME, NULL, 0, NULL);
        }

        case PA_SINK_MESSAGE_SET_SHARED_VOLUME: {
            pa_sink *root_sink = pa_sink_get_master(s);

            if (PA_LIKELY(root_sink))
                set_shared_volume_within_thread(root_sink);

            return 0;
        }

        case PA_SINK_MESSAGE_SET_VOLUME_SYNCED:

            if (s->flags & PA_SINK_DEFERRED_VOLUME) {
                s->set_volume(s);
                pa_sink_volume_change_push(s);
            }
            /* Fall through ... */

        case PA_SINK_MESSAGE_SET_VOLUME:

            if (!pa_cvolume_equal(&s->thread_info.soft_volume, &s->soft_volume)) {
                s->thread_info.soft_volume = s->soft_volume;
                pa_sink_request_rewind(s, (size_t) -1);
            }

            /* Fall through ... */

        case PA_SINK_MESSAGE_SYNC_VOLUMES:
            sync_input_volumes_within_thread(s);
            return 0;

        case PA_SINK_MESSAGE_GET_VOLUME:

            if ((s->flags & PA_SINK_DEFERRED_VOLUME) && s->get_volume) {
                s->get_volume(s);
                pa_sink_volume_change_flush(s);
                pa_sw_cvolume_divide(&s->thread_info.current_hw_volume, &s->real_volume, &s->soft_volume);
            }

            /* In case sink implementor reset SW volume. */
            if (!pa_cvolume_equal(&s->thread_info.soft_volume, &s->soft_volume)) {
                s->thread_info.soft_volume = s->soft_volume;
                pa_sink_request_rewind(s, (size_t) -1);
            }

            return 0;

        case PA_SINK_MESSAGE_SET_MUTE:

            if (s->thread_info.soft_muted != s->muted) {
                s->thread_info.soft_muted = s->muted;
                pa_sink_request_rewind(s, (size_t) -1);
            }

            if (s->flags & PA_SINK_DEFERRED_VOLUME && s->set_mute)
                s->set_mute(s);

            return 0;

        case PA_SINK_MESSAGE_GET_MUTE:

            if (s->flags & PA_SINK_DEFERRED_VOLUME && s->get_mute)
                return s->get_mute(s, userdata);

            return 0;

        case PA_SINK_MESSAGE_SET_STATE: {
            struct set_state_data *data = userdata;
            bool suspend_change =
                (s->thread_info.state == PA_SINK_SUSPENDED && PA_SINK_IS_OPENED(data->state)) ||
                (PA_SINK_IS_OPENED(s->thread_info.state) && data->state == PA_SINK_SUSPENDED);

            if (s->set_state_in_io_thread) {
                int r;

                if ((r = s->set_state_in_io_thread(s, data->state, data->suspend_cause)) < 0)
                    return r;
            }

            s->thread_info.state = data->state;

            if (s->thread_info.state == PA_SINK_SUSPENDED) {
                s->thread_info.rewind_nbytes = 0;
                s->thread_info.rewind_requested = false;
            }

            if (suspend_change) {
                pa_sink_input *i;
                void *state = NULL;

                while ((i = pa_hashmap_iterate(s->thread_info.inputs, &state, NULL)))
                    if (i->suspend_within_thread)
                        i->suspend_within_thread(i, s->thread_info.state == PA_SINK_SUSPENDED);
            }

            return 0;
        }

        case PA_SINK_MESSAGE_GET_REQUESTED_LATENCY: {

            pa_usec_t *usec = userdata;
            *usec = pa_sink_get_requested_latency_within_thread(s);

            /* Yes, that's right, the IO thread will see -1 when no
             * explicit requested latency is configured, the main
             * thread will see max_latency */
            if (*usec == (pa_usec_t) -1)
                *usec = s->thread_info.max_latency;

            return 0;
        }

        case PA_SINK_MESSAGE_SET_LATENCY_RANGE: {
            pa_usec_t *r = userdata;

            pa_sink_set_latency_range_within_thread(s, r[0], r[1]);

            return 0;
        }

        case PA_SINK_MESSAGE_GET_LATENCY_RANGE: {
            pa_usec_t *r = userdata;

            r[0] = s->thread_info.min_latency;
            r[1] = s->thread_info.max_latency;

            return 0;
        }

        case PA_SINK_MESSAGE_GET_FIXED_LATENCY:

            *((pa_usec_t*) userdata) = s->thread_info.fixed_latency;
            return 0;

        case PA_SINK_MESSAGE_SET_FIXED_LATENCY:

            pa_sink_set_fixed_latency_within_thread(s, (pa_usec_t) offset);
            return 0;

        case PA_SINK_MESSAGE_GET_MAX_REWIND:

            *((size_t*) userdata) = s->thread_info.max_rewind;
            return 0;

        case PA_SINK_MESSAGE_GET_MAX_REQUEST:

            *((size_t*) userdata) = s->thread_info.max_request;
            return 0;

        case PA_SINK_MESSAGE_SET_MAX_REWIND:

            pa_sink_set_max_rewind_within_thread(s, (size_t) offset);
            return 0;

        case PA_SINK_MESSAGE_SET_MAX_REQUEST:

            pa_sink_set_max_request_within_thread(s, (size_t) offset);
            return 0;

        case PA_SINK_MESSAGE_UPDATE_VOLUME_AND_MUTE:
            /* This message is sent from IO-thread and handled in main thread. */
            pa_assert_ctl_context();

            /* Make sure we're not messing with main thread when no longer linked */
            if (!PA_SINK_IS_LINKED(s->state))
                return 0;

            pa_sink_get_volume(s, true);
            pa_sink_get_mute(s, true);
            return 0;

        case PA_SINK_MESSAGE_SET_PORT_LATENCY_OFFSET:
            s->thread_info.port_latency_offset = offset;
            return 0;

        case PA_SINK_MESSAGE_GET_LATENCY:
        case PA_SINK_MESSAGE_MAX:
            ;
    }

    return -1;
}

/* Called from main thread */
int pa_sink_suspend_all(pa_core *c, bool suspend, pa_suspend_cause_t cause) {
    pa_sink *sink;
    uint32_t idx;
    int ret = 0;

    pa_core_assert_ref(c);
    pa_assert_ctl_context();
    pa_assert(cause != 0);

    PA_IDXSET_FOREACH(sink, c->sinks, idx) {
        int r;

        if ((r = pa_sink_suspend(sink, suspend, cause)) < 0)
            ret = r;
    }

    return ret;
}

/* Called from IO thread */
void pa_sink_detach_within_thread(pa_sink *s) {
    pa_sink_input *i;
    void *state = NULL;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(PA_SINK_IS_LINKED(s->thread_info.state));

    PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state)
        pa_sink_input_detach(i);

    if (s->monitor_source)
        pa_source_detach_within_thread(s->monitor_source);
}

/* Called from IO thread */
void pa_sink_attach_within_thread(pa_sink *s) {
    pa_sink_input *i;
    void *state = NULL;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(PA_SINK_IS_LINKED(s->thread_info.state));

    PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state)
        pa_sink_input_attach(i);

    if (s->monitor_source)
        pa_source_attach_within_thread(s->monitor_source);
}

/* Called from IO thread */
void pa_sink_request_rewind(pa_sink*s, size_t nbytes) {
    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);
    pa_assert(PA_SINK_IS_LINKED(s->thread_info.state));

    if (nbytes == (size_t) -1)
        nbytes = s->thread_info.max_rewind;

    nbytes = PA_MIN(nbytes, s->thread_info.max_rewind);

    if (s->thread_info.rewind_requested &&
        nbytes <= s->thread_info.rewind_nbytes)
        return;

    s->thread_info.rewind_nbytes = nbytes;
    s->thread_info.rewind_requested = true;

    if (s->request_rewind)
        s->request_rewind(s);
}

/* Called from IO thread */
pa_usec_t pa_sink_get_requested_latency_within_thread(pa_sink *s) {
    pa_usec_t result = (pa_usec_t) -1;
    pa_sink_input *i;
    void *state = NULL;
    pa_usec_t monitor_latency;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);

    if (!(s->flags & PA_SINK_DYNAMIC_LATENCY))
        return PA_CLAMP(s->thread_info.fixed_latency, s->thread_info.min_latency, s->thread_info.max_latency);

    if (s->thread_info.requested_latency_valid)
        return s->thread_info.requested_latency;

    PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state)
        if (i->thread_info.requested_sink_latency != (pa_usec_t) -1 &&
            (result == (pa_usec_t) -1 || result > i->thread_info.requested_sink_latency))
            result = i->thread_info.requested_sink_latency;

    monitor_latency = pa_source_get_requested_latency_within_thread(s->monitor_source);

    if (monitor_latency != (pa_usec_t) -1 &&
        (result == (pa_usec_t) -1 || result > monitor_latency))
        result = monitor_latency;

    if (result != (pa_usec_t) -1)
        result = PA_CLAMP(result, s->thread_info.min_latency, s->thread_info.max_latency);

    if (PA_SINK_IS_LINKED(s->thread_info.state)) {
        /* Only cache if properly initialized */
        s->thread_info.requested_latency = result;
        s->thread_info.requested_latency_valid = true;
    }

    return result;
}

/* Called from main thread */
pa_usec_t pa_sink_get_requested_latency(pa_sink *s) {
    pa_usec_t usec = 0;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_IS_LINKED(s->state));

    if (s->state == PA_SINK_SUSPENDED)
        return 0;

    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_GET_REQUESTED_LATENCY, &usec, 0, NULL) == 0);

    return usec;
}

/* Called from IO as well as the main thread -- the latter only before the IO thread started up */
void pa_sink_set_max_rewind_within_thread(pa_sink *s, size_t max_rewind) {
    pa_sink_input *i;
    void *state = NULL;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);

    if (max_rewind == s->thread_info.max_rewind)
        return;

    s->thread_info.max_rewind = max_rewind;

    if (PA_SINK_IS_LINKED(s->thread_info.state))
        PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state)
            pa_sink_input_update_max_rewind(i, s->thread_info.max_rewind);

    if (s->monitor_source)
        pa_source_set_max_rewind_within_thread(s->monitor_source, s->thread_info.max_rewind);
}

/* Called from main thread */
void pa_sink_set_max_rewind(pa_sink *s, size_t max_rewind) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    if (PA_SINK_IS_LINKED(s->state))
        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_SET_MAX_REWIND, NULL, max_rewind, NULL) == 0);
    else
        pa_sink_set_max_rewind_within_thread(s, max_rewind);
}

/* Called from IO as well as the main thread -- the latter only before the IO thread started up */
void pa_sink_set_max_request_within_thread(pa_sink *s, size_t max_request) {
    void *state = NULL;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);

    if (max_request == s->thread_info.max_request)
        return;

    s->thread_info.max_request = max_request;

    if (PA_SINK_IS_LINKED(s->thread_info.state)) {
        pa_sink_input *i;

        PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state)
            pa_sink_input_update_max_request(i, s->thread_info.max_request);
    }
}

/* Called from main thread */
void pa_sink_set_max_request(pa_sink *s, size_t max_request) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    if (PA_SINK_IS_LINKED(s->state))
        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_SET_MAX_REQUEST, NULL, max_request, NULL) == 0);
    else
        pa_sink_set_max_request_within_thread(s, max_request);
}

/* Called from IO thread */
void pa_sink_invalidate_requested_latency(pa_sink *s, bool dynamic) {
    pa_sink_input *i;
    void *state = NULL;

    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);

    if ((s->flags & PA_SINK_DYNAMIC_LATENCY))
        s->thread_info.requested_latency_valid = false;
    else if (dynamic)
        return;

    if (PA_SINK_IS_LINKED(s->thread_info.state)) {

        if (s->update_requested_latency)
            s->update_requested_latency(s);

        PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state)
            if (i->update_sink_requested_latency)
                i->update_sink_requested_latency(i);
    }
}

/* Called from main thread */
void pa_sink_set_latency_range(pa_sink *s, pa_usec_t min_latency, pa_usec_t max_latency) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    /* min_latency == 0:           no limit
     * min_latency anything else:  specified limit
     *
     * Similar for max_latency */

    if (min_latency < ABSOLUTE_MIN_LATENCY)
        min_latency = ABSOLUTE_MIN_LATENCY;

    if (max_latency <= 0 ||
        max_latency > ABSOLUTE_MAX_LATENCY)
        max_latency = ABSOLUTE_MAX_LATENCY;

    pa_assert(min_latency <= max_latency);

    /* Hmm, let's see if someone forgot to set PA_SINK_DYNAMIC_LATENCY here... */
    pa_assert((min_latency == ABSOLUTE_MIN_LATENCY &&
               max_latency == ABSOLUTE_MAX_LATENCY) ||
              (s->flags & PA_SINK_DYNAMIC_LATENCY));

    if (PA_SINK_IS_LINKED(s->state)) {
        pa_usec_t r[2];

        r[0] = min_latency;
        r[1] = max_latency;

        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_SET_LATENCY_RANGE, r, 0, NULL) == 0);
    } else
        pa_sink_set_latency_range_within_thread(s, min_latency, max_latency);
}

/* Called from main thread */
void pa_sink_get_latency_range(pa_sink *s, pa_usec_t *min_latency, pa_usec_t *max_latency) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();
    pa_assert(min_latency);
    pa_assert(max_latency);

    if (PA_SINK_IS_LINKED(s->state)) {
        pa_usec_t r[2] = { 0, 0 };

        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_GET_LATENCY_RANGE, r, 0, NULL) == 0);

        *min_latency = r[0];
        *max_latency = r[1];
    } else {
        *min_latency = s->thread_info.min_latency;
        *max_latency = s->thread_info.max_latency;
    }
}

/* Called from IO thread */
void pa_sink_set_latency_range_within_thread(pa_sink *s, pa_usec_t min_latency, pa_usec_t max_latency) {
    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);

    pa_assert(min_latency >= ABSOLUTE_MIN_LATENCY);
    pa_assert(max_latency <= ABSOLUTE_MAX_LATENCY);
    pa_assert(min_latency <= max_latency);

    /* Hmm, let's see if someone forgot to set PA_SINK_DYNAMIC_LATENCY here... */
    pa_assert((min_latency == ABSOLUTE_MIN_LATENCY &&
               max_latency == ABSOLUTE_MAX_LATENCY) ||
              (s->flags & PA_SINK_DYNAMIC_LATENCY));

    if (s->thread_info.min_latency == min_latency &&
        s->thread_info.max_latency == max_latency)
        return;

    s->thread_info.min_latency = min_latency;
    s->thread_info.max_latency = max_latency;

    if (PA_SINK_IS_LINKED(s->thread_info.state)) {
        pa_sink_input *i;
        void *state = NULL;

        PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state)
            if (i->update_sink_latency_range)
                i->update_sink_latency_range(i);
    }

    pa_sink_invalidate_requested_latency(s, false);

    pa_source_set_latency_range_within_thread(s->monitor_source, min_latency, max_latency);
}

/* Called from main thread */
void pa_sink_set_fixed_latency(pa_sink *s, pa_usec_t latency) {
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    if (s->flags & PA_SINK_DYNAMIC_LATENCY) {
        pa_assert(latency == 0);
        return;
    }

    if (latency < ABSOLUTE_MIN_LATENCY)
        latency = ABSOLUTE_MIN_LATENCY;

    if (latency > ABSOLUTE_MAX_LATENCY)
        latency = ABSOLUTE_MAX_LATENCY;

    if (PA_SINK_IS_LINKED(s->state))
        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_SET_FIXED_LATENCY, NULL, (int64_t) latency, NULL) == 0);
    else
        s->thread_info.fixed_latency = latency;

    pa_source_set_fixed_latency(s->monitor_source, latency);
}

/* Called from main thread */
pa_usec_t pa_sink_get_fixed_latency(pa_sink *s) {
    pa_usec_t latency;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    if (s->flags & PA_SINK_DYNAMIC_LATENCY)
        return 0;

    if (PA_SINK_IS_LINKED(s->state))
        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_GET_FIXED_LATENCY, &latency, 0, NULL) == 0);
    else
        latency = s->thread_info.fixed_latency;

    return latency;
}

/* Called from IO thread */
void pa_sink_set_fixed_latency_within_thread(pa_sink *s, pa_usec_t latency) {
    pa_sink_assert_ref(s);
    pa_sink_assert_io_context(s);

    if (s->flags & PA_SINK_DYNAMIC_LATENCY) {
        pa_assert(latency == 0);
        s->thread_info.fixed_latency = 0;

        if (s->monitor_source)
            pa_source_set_fixed_latency_within_thread(s->monitor_source, 0);

        return;
    }

    pa_assert(latency >= ABSOLUTE_MIN_LATENCY);
    pa_assert(latency <= ABSOLUTE_MAX_LATENCY);

    if (s->thread_info.fixed_latency == latency)
        return;

    s->thread_info.fixed_latency = latency;

    if (PA_SINK_IS_LINKED(s->thread_info.state)) {
        pa_sink_input *i;
        void *state = NULL;

        PA_HASHMAP_FOREACH(i, s->thread_info.inputs, state)
            if (i->update_sink_fixed_latency)
                i->update_sink_fixed_latency(i);
    }

    pa_sink_invalidate_requested_latency(s, false);

    pa_source_set_fixed_latency_within_thread(s->monitor_source, latency);
}

/* Called from main context */
void pa_sink_set_port_latency_offset(pa_sink *s, int64_t offset) {
    pa_sink_assert_ref(s);

    s->port_latency_offset = offset;

    if (PA_SINK_IS_LINKED(s->state))
        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_SET_PORT_LATENCY_OFFSET, NULL, offset, NULL) == 0);
    else
        s->thread_info.port_latency_offset = offset;

    pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_PORT_LATENCY_OFFSET_CHANGED], s);
}

/* Called from main context */
size_t pa_sink_get_max_rewind(pa_sink *s) {
    size_t r;
    pa_assert_ctl_context();
    pa_sink_assert_ref(s);

    if (!PA_SINK_IS_LINKED(s->state))
        return s->thread_info.max_rewind;

    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_GET_MAX_REWIND, &r, 0, NULL) == 0);

    return r;
}

/* Called from main context */
size_t pa_sink_get_max_request(pa_sink *s) {
    size_t r;
    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    if (!PA_SINK_IS_LINKED(s->state))
        return s->thread_info.max_request;

    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SINK_MESSAGE_GET_MAX_REQUEST, &r, 0, NULL) == 0);

    return r;
}

/* Called from main context */
int pa_sink_set_port(pa_sink *s, const char *name, bool save) {
    pa_device_port *port;

    pa_sink_assert_ref(s);
    pa_assert_ctl_context();

    if (!s->set_port) {
        pa_log_debug("set_port() operation not implemented for sink %u \"%s\"", s->index, s->name);
        return -PA_ERR_NOTIMPLEMENTED;
    }

    if (!name)
        return -PA_ERR_NOENTITY;

    if (!(port = pa_hashmap_get(s->ports, name)))
        return -PA_ERR_NOENTITY;

    if (s->active_port == port) {
        s->save_port = s->save_port || save;
        return 0;
    }

    if (s->set_port(s, port) < 0)
        return -PA_ERR_NOENTITY;

    pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);

    pa_log_info("Changed port of sink %u \"%s\" to %s", s->index, s->name, port->name);

    s->active_port = port;
    s->save_port = save;

    pa_sink_set_port_latency_offset(s, s->active_port->latency_offset);

    /* The active port affects the default sink selection. */
    pa_core_update_default_sink(s->core);

    pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_PORT_CHANGED], s);

    return 0;
}

bool pa_device_init_icon(pa_proplist *p, bool is_sink) {
    const char *ff, *c, *t = NULL, *s = "", *profile, *bus;

    pa_assert(p);

    if (pa_proplist_contains(p, PA_PROP_DEVICE_ICON_NAME))
        return true;

    if ((ff = pa_proplist_gets(p, PA_PROP_DEVICE_FORM_FACTOR))) {

        if (pa_streq(ff, "microphone"))
            t = "audio-input-microphone";
        else if (pa_streq(ff, "webcam"))
            t = "camera-web";
        else if (pa_streq(ff, "computer"))
            t = "computer";
        else if (pa_streq(ff, "handset"))
            t = "phone";
        else if (pa_streq(ff, "portable"))
            t = "multimedia-player";
        else if (pa_streq(ff, "tv"))
            t = "video-display";

        /*
         * The following icons are not part of the icon naming spec,
         * because Rodney Dawes sucks as the maintainer of that spec.
         *
         * http://lists.freedesktop.org/archives/xdg/2009-May/010397.html
         */
        else if (pa_streq(ff, "headset"))
            t = "audio-headset";
        else if (pa_streq(ff, "headphone"))
            t = "audio-headphones";
        else if (pa_streq(ff, "speaker"))
            t = "audio-speakers";
        else if (pa_streq(ff, "hands-free"))
            t = "audio-handsfree";
    }

    if (!t)
        if ((c = pa_proplist_gets(p, PA_PROP_DEVICE_CLASS)))
            if (pa_streq(c, "modem"))
                t = "modem";

    if (!t) {
        if (is_sink)
            t = "audio-card";
        else
            t = "audio-input-microphone";
    }

    if ((profile = pa_proplist_gets(p, PA_PROP_DEVICE_PROFILE_NAME))) {
        if (strstr(profile, "analog"))
            s = "-analog";
        else if (strstr(profile, "iec958"))
            s = "-iec958";
        else if (strstr(profile, "hdmi"))
            s = "-hdmi";
    }

    bus = pa_proplist_gets(p, PA_PROP_DEVICE_BUS);

    pa_proplist_setf(p, PA_PROP_DEVICE_ICON_NAME, "%s%s%s%s", t, pa_strempty(s), bus ? "-" : "", pa_strempty(bus));

    return true;
}

bool pa_device_init_description(pa_proplist *p, pa_card *card) {
    const char *s, *d = NULL, *k;
    pa_assert(p);

    if (pa_proplist_contains(p, PA_PROP_DEVICE_DESCRIPTION))
        return true;

    if (card)
        if ((s = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION)))
            d = s;

    if (!d)
        if ((s = pa_proplist_gets(p, PA_PROP_DEVICE_FORM_FACTOR)))
            if (pa_streq(s, "internal"))
                d = _("Built-in Audio");

    if (!d)
        if ((s = pa_proplist_gets(p, PA_PROP_DEVICE_CLASS)))
            if (pa_streq(s, "modem"))
                d = _("Modem");

    if (!d)
        d = pa_proplist_gets(p, PA_PROP_DEVICE_PRODUCT_NAME);

    if (!d)
        return false;

    k = pa_proplist_gets(p, PA_PROP_DEVICE_PROFILE_DESCRIPTION);

    if (d && k)
        pa_proplist_setf(p, PA_PROP_DEVICE_DESCRIPTION, "%s %s", d, k);
    else if (d)
        pa_proplist_sets(p, PA_PROP_DEVICE_DESCRIPTION, d);

    return true;
}

bool pa_device_init_intended_roles(pa_proplist *p) {
    const char *s;
    pa_assert(p);

    if (pa_proplist_contains(p, PA_PROP_DEVICE_INTENDED_ROLES))
        return true;

    if ((s = pa_proplist_gets(p, PA_PROP_DEVICE_FORM_FACTOR)))
        if (pa_streq(s, "handset") || pa_streq(s, "hands-free")
            || pa_streq(s, "headset")) {
            pa_proplist_sets(p, PA_PROP_DEVICE_INTENDED_ROLES, "phone");
            return true;
        }

    return false;
}

unsigned pa_device_init_priority(pa_proplist *p) {
    const char *s;
    unsigned priority = 0;

    pa_assert(p);

    if ((s = pa_proplist_gets(p, PA_PROP_DEVICE_CLASS))) {

        if (pa_streq(s, "sound"))
            priority += 9000;
        else if (!pa_streq(s, "modem"))
            priority += 1000;
    }

    if ((s = pa_proplist_gets(p, PA_PROP_DEVICE_FORM_FACTOR))) {

        if (pa_streq(s, "headphone"))
            priority += 900;
        else if (pa_streq(s, "hifi"))
            priority += 600;
        else if (pa_streq(s, "speaker"))
            priority += 500;
        else if (pa_streq(s, "portable"))
            priority += 450;
    }

    if ((s = pa_proplist_gets(p, PA_PROP_DEVICE_BUS))) {

        if (pa_streq(s, "bluetooth"))
            priority += 50;
        else if (pa_streq(s, "usb"))
            priority += 40;
        else if (pa_streq(s, "pci"))
            priority += 30;
    }

    if ((s = pa_proplist_gets(p, PA_PROP_DEVICE_PROFILE_NAME))) {

        if (pa_startswith(s, "analog-"))
            priority += 9;
        else if (pa_startswith(s, "iec958-"))
            priority += 8;
    }

    return priority;
}

PA_STATIC_FLIST_DECLARE(pa_sink_volume_change, 0, pa_xfree);

/* Called from the IO thread. */
static pa_sink_volume_change *pa_sink_volume_change_new(pa_sink *s) {
    pa_sink_volume_change *c;
    if (!(c = pa_flist_pop(PA_STATIC_FLIST_GET(pa_sink_volume_change))))
        c = pa_xnew(pa_sink_volume_change, 1);

    PA_LLIST_INIT(pa_sink_volume_change, c);
    c->at = 0;
    pa_cvolume_reset(&c->hw_volume, s->sample_spec.channels);
    return c;
}

/* Called from the IO thread. */
static void pa_sink_volume_change_free(pa_sink_volume_change *c) {
    pa_assert(c);
    if (pa_flist_push(PA_STATIC_FLIST_GET(pa_sink_volume_change), c) < 0)
        pa_xfree(c);
}

/* Called from the IO thread. */
void pa_sink_volume_change_push(pa_sink *s) {
    pa_sink_volume_change *c = NULL;
    pa_sink_volume_change *nc = NULL;
    pa_sink_volume_change *pc = NULL;
    uint32_t safety_margin = s->thread_info.volume_change_safety_margin;

    const char *direction = NULL;

    pa_assert(s);
    nc = pa_sink_volume_change_new(s);

    /* NOTE: There is already more different volumes in pa_sink that I can remember.
     *       Adding one more volume for HW would get us rid of this, but I am trying
     *       to survive with the ones we already have. */
    pa_sw_cvolume_divide(&nc->hw_volume, &s->real_volume, &s->soft_volume);

    if (!s->thread_info.volume_changes && pa_cvolume_equal(&nc->hw_volume, &s->thread_info.current_hw_volume)) {
        pa_log_debug("Volume not changing");
        pa_sink_volume_change_free(nc);
        return;
    }

    nc->at = pa_sink_get_latency_within_thread(s, false);
    nc->at += pa_rtclock_now() + s->thread_info.volume_change_extra_delay;

    if (s->thread_info.volume_changes_tail) {
        for (c = s->thread_info.volume_changes_tail; c; c = c->prev) {
            /* If volume is going up let's do it a bit late. If it is going
             * down let's do it a bit early. */
            if (pa_cvolume_avg(&nc->hw_volume) > pa_cvolume_avg(&c->hw_volume)) {
                if (nc->at + safety_margin > c->at) {
                    nc->at += safety_margin;
                    direction = "up";
                    break;
                }
            }
            else if (nc->at - safety_margin > c->at) {
                    nc->at -= safety_margin;
                    direction = "down";
                    break;
            }
        }
    }

    if (c == NULL) {
        if (pa_cvolume_avg(&nc->hw_volume) > pa_cvolume_avg(&s->thread_info.current_hw_volume)) {
            nc->at += safety_margin;
            direction = "up";
        } else {
            nc->at -= safety_margin;
            direction = "down";
        }
        PA_LLIST_PREPEND(pa_sink_volume_change, s->thread_info.volume_changes, nc);
    }
    else {
        PA_LLIST_INSERT_AFTER(pa_sink_volume_change, s->thread_info.volume_changes, c, nc);
    }

    pa_log_debug("Volume going %s to %d at %llu", direction, pa_cvolume_avg(&nc->hw_volume), (long long unsigned) nc->at);

    /* We can ignore volume events that came earlier but should happen later than this. */
    PA_LLIST_FOREACH_SAFE(c, pc, nc->next) {
        pa_log_debug("Volume change to %d at %llu was dropped", pa_cvolume_avg(&c->hw_volume), (long long unsigned) c->at);
        pa_sink_volume_change_free(c);
    }
    nc->next = NULL;
    s->thread_info.volume_changes_tail = nc;
}

/* Called from the IO thread. */
static void pa_sink_volume_change_flush(pa_sink *s) {
    pa_sink_volume_change *c = s->thread_info.volume_changes;
    pa_assert(s);
    s->thread_info.volume_changes = NULL;
    s->thread_info.volume_changes_tail = NULL;
    while (c) {
        pa_sink_volume_change *next = c->next;
        pa_sink_volume_change_free(c);
        c = next;
    }
}

/* Called from the IO thread. */
bool pa_sink_volume_change_apply(pa_sink *s, pa_usec_t *usec_to_next) {
    pa_usec_t now;
    bool ret = false;

    pa_assert(s);

    if (!s->thread_info.volume_changes || !PA_SINK_IS_LINKED(s->state)) {
        if (usec_to_next)
            *usec_to_next = 0;
        return ret;
    }

    pa_assert(s->write_volume);

    now = pa_rtclock_now();

    while (s->thread_info.volume_changes && now >= s->thread_info.volume_changes->at) {
        pa_sink_volume_change *c = s->thread_info.volume_changes;
        PA_LLIST_REMOVE(pa_sink_volume_change, s->thread_info.volume_changes, c);
        pa_log_debug("Volume change to %d at %llu was written %llu usec late",
                     pa_cvolume_avg(&c->hw_volume), (long long unsigned) c->at, (long long unsigned) (now - c->at));
        ret = true;
        s->thread_info.current_hw_volume = c->hw_volume;
        pa_sink_volume_change_free(c);
    }

    if (ret)
        s->write_volume(s);

    if (s->thread_info.volume_changes) {
        if (usec_to_next)
            *usec_to_next = s->thread_info.volume_changes->at - now;
        if (pa_log_ratelimit(PA_LOG_DEBUG))
            pa_log_debug("Next volume change in %lld usec", (long long) (s->thread_info.volume_changes->at - now));
    }
    else {
        if (usec_to_next)
            *usec_to_next = 0;
        s->thread_info.volume_changes_tail = NULL;
    }
    return ret;
}

/* Called from the IO thread. */
static void pa_sink_volume_change_rewind(pa_sink *s, size_t nbytes) {
    /* All the queued volume events later than current latency are shifted to happen earlier. */
    pa_sink_volume_change *c;
    pa_volume_t prev_vol = pa_cvolume_avg(&s->thread_info.current_hw_volume);
    pa_usec_t rewound = pa_bytes_to_usec(nbytes, &s->sample_spec);
    pa_usec_t limit = pa_sink_get_latency_within_thread(s, false);

    pa_log_debug("latency = %lld", (long long) limit);
    limit += pa_rtclock_now() + s->thread_info.volume_change_extra_delay;

    PA_LLIST_FOREACH(c, s->thread_info.volume_changes) {
        pa_usec_t modified_limit = limit;
        if (prev_vol > pa_cvolume_avg(&c->hw_volume))
            modified_limit -= s->thread_info.volume_change_safety_margin;
        else
            modified_limit += s->thread_info.volume_change_safety_margin;
        if (c->at > modified_limit) {
            c->at -= rewound;
            if (c->at < modified_limit)
                c->at = modified_limit;
        }
        prev_vol = pa_cvolume_avg(&c->hw_volume);
    }
    pa_sink_volume_change_apply(s, NULL);
}

/* Called from the main thread */
/* Gets the list of formats supported by the sink. The members and idxset must
 * be freed by the caller. */
pa_idxset* pa_sink_get_formats(pa_sink *s) {
    pa_idxset *ret;

    pa_assert(s);

    if (s->get_formats) {
        /* Sink supports format query, all is good */
        ret = s->get_formats(s);
    } else {
        /* Sink doesn't support format query, so assume it does PCM */
        pa_format_info *f = pa_format_info_new();
        f->encoding = PA_ENCODING_PCM;

        ret = pa_idxset_new(NULL, NULL);
        pa_idxset_put(ret, f, NULL);
    }

    return ret;
}

/* Called from the main thread */
/* Allows an external source to set what formats a sink supports if the sink
 * permits this. The function makes a copy of the formats on success. */
bool pa_sink_set_formats(pa_sink *s, pa_idxset *formats) {
    pa_assert(s);
    pa_assert(formats);

    if (s->set_formats)
        /* Sink supports setting formats -- let's give it a shot */
        return s->set_formats(s, formats);
    else
        /* Sink doesn't support setting this -- bail out */
        return false;
}

/* Called from the main thread */
/* Checks if the sink can accept this format */
bool pa_sink_check_format(pa_sink *s, pa_format_info *f) {
    pa_idxset *formats = NULL;
    bool ret = false;

    pa_assert(s);
    pa_assert(f);

    formats = pa_sink_get_formats(s);

    if (formats) {
        pa_format_info *finfo_device;
        uint32_t i;

        PA_IDXSET_FOREACH(finfo_device, formats, i) {
            if (pa_format_info_is_compatible(finfo_device, f)) {
                ret = true;
                break;
            }
        }

        pa_idxset_free(formats, (pa_free_cb_t) pa_format_info_free);
    }

    return ret;
}

/* Called from the main thread */
/* Calculates the intersection between formats supported by the sink and
 * in_formats, and returns these, in the order of the sink's formats. */
pa_idxset* pa_sink_check_formats(pa_sink *s, pa_idxset *in_formats) {
    pa_idxset *out_formats = pa_idxset_new(NULL, NULL), *sink_formats = NULL;
    pa_format_info *f_sink, *f_in;
    uint32_t i, j;

    pa_assert(s);

    if (!in_formats || pa_idxset_isempty(in_formats))
        goto done;

    sink_formats = pa_sink_get_formats(s);

    PA_IDXSET_FOREACH(f_sink, sink_formats, i) {
        PA_IDXSET_FOREACH(f_in, in_formats, j) {
            if (pa_format_info_is_compatible(f_sink, f_in))
                pa_idxset_put(out_formats, pa_format_info_copy(f_in), NULL);
        }
    }

done:
    if (sink_formats)
        pa_idxset_free(sink_formats, (pa_free_cb_t) pa_format_info_free);

    return out_formats;
}

/* Called from the main thread */
void pa_sink_set_sample_format(pa_sink *s, pa_sample_format_t format) {
    pa_sample_format_t old_format;

    pa_assert(s);
    pa_assert(pa_sample_format_valid(format));

    old_format = s->sample_spec.format;
    if (old_format == format)
        return;

    pa_log_info("%s: format: %s -> %s",
                s->name, pa_sample_format_to_string(old_format), pa_sample_format_to_string(format));

    s->sample_spec.format = format;

    pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK | PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
}

/* Called from the main thread */
void pa_sink_set_sample_rate(pa_sink *s, uint32_t rate) {
    uint32_t old_rate;

    pa_assert(s);
    pa_assert(pa_sample_rate_valid(rate));

    old_rate = s->sample_spec.rate;
    if (old_rate == rate)
        return;

    pa_log_info("%s: rate: %u -> %u", s->name, old_rate, rate);

    s->sample_spec.rate = rate;

    pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK | PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
}

/* Called from the main thread. */
void pa_sink_set_reference_volume_direct(pa_sink *s, const pa_cvolume *volume) {
    pa_cvolume old_volume;
    char old_volume_str[PA_CVOLUME_SNPRINT_VERBOSE_MAX];
    char new_volume_str[PA_CVOLUME_SNPRINT_VERBOSE_MAX];

    pa_assert(s);
    pa_assert(volume);

    old_volume = s->reference_volume;

    if (pa_cvolume_equal(volume, &old_volume))
        return;

    s->reference_volume = *volume;
    pa_log_debug("The reference volume of sink %s changed from %s to %s.", s->name,
                 pa_cvolume_snprint_verbose(old_volume_str, sizeof(old_volume_str), &old_volume, &s->channel_map,
                                            s->flags & PA_SINK_DECIBEL_VOLUME),
                 pa_cvolume_snprint_verbose(new_volume_str, sizeof(new_volume_str), volume, &s->channel_map,
                                            s->flags & PA_SINK_DECIBEL_VOLUME));

    pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
    pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SINK_VOLUME_CHANGED], s);
}

void pa_sink_move_streams_to_default_sink(pa_core *core, pa_sink *old_sink, bool default_sink_changed) {
    pa_sink_input *i;
    uint32_t idx;

    pa_assert(core);
    pa_assert(old_sink);

    if (core->state == PA_CORE_SHUTDOWN)
        return;

    if (core->default_sink == NULL || core->default_sink->unlink_requested)
        return;

    if (old_sink == core->default_sink)
        return;

    PA_IDXSET_FOREACH(i, old_sink->inputs, idx) {
        if (!PA_SINK_INPUT_IS_LINKED(i->state))
            continue;

        if (!i->sink)
            continue;

        /* Don't move sink-inputs which connect filter sinks to their target sinks */
        if (i->origin_sink)
            continue;

        /* If default_sink_changed is false, the old sink became unavailable, so all streams must be moved. */
        if (pa_safe_streq(old_sink->name, i->preferred_sink) && default_sink_changed)
            continue;

        if (!pa_sink_input_may_move_to(i, core->default_sink))
            continue;

        if (default_sink_changed)
            pa_log_info("The sink input %u \"%s\" is moving to %s due to change of the default sink.",
                        i->index, pa_strnull(pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME)), core->default_sink->name);
        else
            pa_log_info("The sink input %u \"%s\" is moving to %s, because the old sink became unavailable.",
                        i->index, pa_strnull(pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME)), core->default_sink->name);

        pa_sink_input_move_to(i, core->default_sink, false);
    }
}
