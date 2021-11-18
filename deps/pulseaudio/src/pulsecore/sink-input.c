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

#include <pulse/utf8.h>
#include <pulse/xmalloc.h>
#include <pulse/util.h>
#include <pulse/internal.h>

#include <pulsecore/core-format.h>
#include <pulsecore/mix.h>
#include <pulsecore/stream-util.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/log.h>
#include <pulsecore/play-memblockq.h>
#include <pulsecore/namereg.h>
#include <pulsecore/core-util.h>

#include "sink-input.h"

/* #define SINK_INPUT_DEBUG */

#define MEMBLOCKQ_MAXLENGTH (32*1024*1024)
#define CONVERT_BUFFER_LENGTH (pa_page_size())

PA_DEFINE_PUBLIC_CLASS(pa_sink_input, pa_msgobject);

struct volume_factor_entry {
    char *key;
    pa_cvolume volume;
};

static struct volume_factor_entry *volume_factor_entry_new(const char *key, const pa_cvolume *volume) {
    struct volume_factor_entry *entry;

    pa_assert(key);
    pa_assert(volume);

    entry = pa_xnew(struct volume_factor_entry, 1);
    entry->key = pa_xstrdup(key);

    entry->volume = *volume;

    return entry;
}

static void volume_factor_entry_free(struct volume_factor_entry *volume_entry) {
    pa_assert(volume_entry);

    pa_xfree(volume_entry->key);
    pa_xfree(volume_entry);
}

static void volume_factor_from_hashmap(pa_cvolume *v, pa_hashmap *items, uint8_t channels) {
    struct volume_factor_entry *entry;
    void *state = NULL;

    pa_cvolume_reset(v, channels);
    PA_HASHMAP_FOREACH(entry, items, state)
        pa_sw_cvolume_multiply(v, v, &entry->volume);
}

static void sink_input_free(pa_object *o);
static void set_real_ratio(pa_sink_input *i, const pa_cvolume *v);

static int check_passthrough_connection(bool passthrough, pa_sink *dest) {
    if (pa_sink_is_passthrough(dest)) {
        pa_log_warn("Sink is already connected to PASSTHROUGH input");
        return -PA_ERR_BUSY;
    }

    /* If current input(s) exist, check new input is not PASSTHROUGH */
    if (pa_idxset_size(dest->inputs) > 0 && passthrough) {
        pa_log_warn("Sink is already connected, cannot accept new PASSTHROUGH INPUT");
        return -PA_ERR_BUSY;
    }

    return PA_OK;
}

pa_sink_input_new_data* pa_sink_input_new_data_init(pa_sink_input_new_data *data) {
    pa_assert(data);

    pa_zero(*data);
    data->resample_method = PA_RESAMPLER_INVALID;
    data->proplist = pa_proplist_new();
    data->volume_writable = true;

    data->volume_factor_items = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                                    (pa_free_cb_t) volume_factor_entry_free);
    data->volume_factor_sink_items = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                                         (pa_free_cb_t) volume_factor_entry_free);

    return data;
}

void pa_sink_input_new_data_set_sample_spec(pa_sink_input_new_data *data, const pa_sample_spec *spec) {
    pa_assert(data);

    if ((data->sample_spec_is_set = !!spec))
        data->sample_spec = *spec;
}

void pa_sink_input_new_data_set_channel_map(pa_sink_input_new_data *data, const pa_channel_map *map) {
    pa_assert(data);

    if ((data->channel_map_is_set = !!map))
        data->channel_map = *map;
}

bool pa_sink_input_new_data_is_passthrough(pa_sink_input_new_data *data) {
    pa_assert(data);

    if (PA_LIKELY(data->format) && PA_UNLIKELY(!pa_format_info_is_pcm(data->format)))
        return true;

    if (PA_UNLIKELY(data->flags & PA_SINK_INPUT_PASSTHROUGH))
        return true;

    return false;
}

void pa_sink_input_new_data_set_volume(pa_sink_input_new_data *data, const pa_cvolume *volume) {
    pa_assert(data);
    pa_assert(data->volume_writable);

    if ((data->volume_is_set = !!volume))
        data->volume = *volume;
}

void pa_sink_input_new_data_add_volume_factor(pa_sink_input_new_data *data, const char *key, const pa_cvolume *volume_factor) {
    struct volume_factor_entry *v;

    pa_assert(data);
    pa_assert(key);
    pa_assert(volume_factor);

    v = volume_factor_entry_new(key, volume_factor);
    pa_assert_se(pa_hashmap_put(data->volume_factor_items, v->key, v) >= 0);
}

void pa_sink_input_new_data_add_volume_factor_sink(pa_sink_input_new_data *data, const char *key, const pa_cvolume *volume_factor) {
    struct volume_factor_entry *v;

    pa_assert(data);
    pa_assert(key);
    pa_assert(volume_factor);

    v = volume_factor_entry_new(key, volume_factor);
    pa_assert_se(pa_hashmap_put(data->volume_factor_sink_items, v->key, v) >= 0);
}

void pa_sink_input_new_data_set_muted(pa_sink_input_new_data *data, bool mute) {
    pa_assert(data);

    data->muted_is_set = true;
    data->muted = mute;
}

bool pa_sink_input_new_data_set_sink(pa_sink_input_new_data *data, pa_sink *s, bool save, bool requested_by_application) {
    bool ret = true;
    pa_idxset *formats = NULL;

    pa_assert(data);
    pa_assert(s);

    if (!data->req_formats) {
        /* We're not working with the extended API */
        data->sink = s;
        if (save) {
            pa_xfree(data->preferred_sink);
            data->preferred_sink = pa_xstrdup(s->name);
	}
        data->sink_requested_by_application = requested_by_application;
    } else {
        /* Extended API: let's see if this sink supports the formats the client can provide */
        formats = pa_sink_check_formats(s, data->req_formats);

        if (formats && !pa_idxset_isempty(formats)) {
            /* Sink supports at least one of the requested formats */
            data->sink = s;
	    if (save) {
		pa_xfree(data->preferred_sink);
		data->preferred_sink = pa_xstrdup(s->name);
	    }
            data->sink_requested_by_application = requested_by_application;
            if (data->nego_formats)
                pa_idxset_free(data->nego_formats, (pa_free_cb_t) pa_format_info_free);
            data->nego_formats = formats;
        } else {
            /* Sink doesn't support any of the formats requested by the client */
            if (formats)
                pa_idxset_free(formats, (pa_free_cb_t) pa_format_info_free);
            ret = false;
        }
    }

    return ret;
}

bool pa_sink_input_new_data_set_formats(pa_sink_input_new_data *data, pa_idxset *formats) {
    pa_assert(data);
    pa_assert(formats);

    if (data->req_formats)
        pa_idxset_free(data->req_formats, (pa_free_cb_t) pa_format_info_free);

    data->req_formats = formats;

    if (data->sink) {
        /* Trigger format negotiation */
        return pa_sink_input_new_data_set_sink(data, data->sink, (data->preferred_sink != NULL), data->sink_requested_by_application);
    }

    return true;
}

void pa_sink_input_new_data_done(pa_sink_input_new_data *data) {
    pa_assert(data);

    if (data->req_formats)
        pa_idxset_free(data->req_formats, (pa_free_cb_t) pa_format_info_free);

    if (data->nego_formats)
        pa_idxset_free(data->nego_formats, (pa_free_cb_t) pa_format_info_free);

    if (data->format)
        pa_format_info_free(data->format);

    if (data->volume_factor_items)
        pa_hashmap_free(data->volume_factor_items);

    if (data->volume_factor_sink_items)
        pa_hashmap_free(data->volume_factor_sink_items);

    if (data->preferred_sink)
        pa_xfree(data->preferred_sink);

    pa_proplist_free(data->proplist);
}

/* Called from main context */
static void reset_callbacks(pa_sink_input *i) {
    pa_assert(i);

    i->pop = NULL;
    i->process_underrun = NULL;
    i->process_rewind = NULL;
    i->update_max_rewind = NULL;
    i->update_max_request = NULL;
    i->update_sink_requested_latency = NULL;
    i->update_sink_latency_range = NULL;
    i->update_sink_fixed_latency = NULL;
    i->attach = NULL;
    i->detach = NULL;
    i->suspend = NULL;
    i->suspend_within_thread = NULL;
    i->moving = NULL;
    i->kill = NULL;
    i->get_latency = NULL;
    i->state_change = NULL;
    i->may_move_to = NULL;
    i->send_event = NULL;
    i->volume_changed = NULL;
    i->mute_changed = NULL;
}

/* Called from main context */
int pa_sink_input_new(
        pa_sink_input **_i,
        pa_core *core,
        pa_sink_input_new_data *data) {

    pa_sink_input *i;
    pa_resampler *resampler = NULL;
    char st[PA_SAMPLE_SPEC_SNPRINT_MAX], cm[PA_CHANNEL_MAP_SNPRINT_MAX], fmt[PA_FORMAT_INFO_SNPRINT_MAX];
    pa_channel_map volume_map;
    int r;
    char *pt;
    char *memblockq_name;

    pa_assert(_i);
    pa_assert(core);
    pa_assert(data);
    pa_assert_ctl_context();

    if (data->client)
        pa_proplist_update(data->proplist, PA_UPDATE_MERGE, data->client->proplist);

    if (data->origin_sink && (data->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER))
        data->volume_writable = false;

    if (!data->req_formats) {
        /* From this point on, we want to work only with formats, and get back
         * to using the sample spec and channel map after all decisions w.r.t.
         * routing are complete. */
        pa_format_info *f;
        pa_idxset *formats;

        f = pa_format_info_from_sample_spec2(&data->sample_spec, data->channel_map_is_set ? &data->channel_map : NULL,
                                             !(data->flags & PA_SINK_INPUT_FIX_FORMAT),
                                             !(data->flags & PA_SINK_INPUT_FIX_RATE),
                                             !(data->flags & PA_SINK_INPUT_FIX_CHANNELS));
        if (!f)
            return -PA_ERR_INVALID;

        formats = pa_idxset_new(NULL, NULL);
        pa_idxset_put(formats, f, NULL);
        pa_sink_input_new_data_set_formats(data, formats);
    }

    if ((r = pa_hook_fire(&core->hooks[PA_CORE_HOOK_SINK_INPUT_NEW], data)) < 0)
        return r;

    pa_return_val_if_fail(!data->driver || pa_utf8_valid(data->driver), -PA_ERR_INVALID);

    if (!data->sink) {
        pa_sink *sink = pa_namereg_get(core, NULL, PA_NAMEREG_SINK);
        pa_return_val_if_fail(sink, -PA_ERR_NOENTITY);
        pa_sink_input_new_data_set_sink(data, sink, false, false);
    }

    /* If something didn't pick a format for us, pick the top-most format since
     * we assume this is sorted in priority order */
    if (!data->format && data->nego_formats && !pa_idxset_isempty(data->nego_formats))
        data->format = pa_format_info_copy(pa_idxset_first(data->nego_formats, NULL));

    if (PA_LIKELY(data->format)) {
        /* We know that data->sink is set, because data->format has been set.
         * data->format is set after a successful format negotiation, and that
         * can't happen before data->sink has been set. */
        pa_assert(data->sink);

        pa_log_debug("Negotiated format: %s", pa_format_info_snprint(fmt, sizeof(fmt), data->format));
    } else {
        pa_format_info *format;
        uint32_t idx;

        pa_log_info("Sink does not support any requested format:");
        PA_IDXSET_FOREACH(format, data->req_formats, idx)
            pa_log_info(" -- %s", pa_format_info_snprint(fmt, sizeof(fmt), format));

        return -PA_ERR_NOTSUPPORTED;
    }

    pa_return_val_if_fail(PA_SINK_IS_LINKED(data->sink->state), -PA_ERR_BADSTATE);
    pa_return_val_if_fail(!data->sync_base || (data->sync_base->sink == data->sink
                                               && data->sync_base->state == PA_SINK_INPUT_CORKED),
                          -PA_ERR_INVALID);

    /* Routing is done. We have a sink and a format. */

    if (data->volume_is_set && !pa_sink_input_new_data_is_passthrough(data)) {
        /* If volume is set, we need to save the original data->channel_map,
         * so that we can remap the volume from the original channel map to the
         * final channel map of the stream in case data->channel_map gets
         * modified in pa_format_info_to_sample_spec2(). */
        r = pa_stream_get_volume_channel_map(&data->volume, data->channel_map_is_set ? &data->channel_map : NULL, data->format, &volume_map);
        if (r < 0)
            return r;
    } else {
        /* Initialize volume_map to invalid state. We check the state later to
         * determine if volume remapping is needed. */
        pa_channel_map_init(&volume_map);
    }

    /* Now populate the sample spec and channel map according to the final
     * format that we've negotiated */
    r = pa_format_info_to_sample_spec2(data->format, &data->sample_spec, &data->channel_map, &data->sink->sample_spec,
                                       &data->sink->channel_map);
    if (r < 0)
        return r;

    r = check_passthrough_connection(pa_sink_input_new_data_is_passthrough(data), data->sink);
    if (r != PA_OK)
        return r;

    /* Don't restore (or save) stream volume for passthrough streams and
     * prevent attenuation/gain */
    if (pa_sink_input_new_data_is_passthrough(data)) {
        data->volume_is_set = true;
        pa_cvolume_reset(&data->volume, data->sample_spec.channels);
        data->volume_is_absolute = true;
        data->save_volume = false;
    }

    if (!data->volume_is_set) {
        pa_cvolume_reset(&data->volume, data->sample_spec.channels);
        data->volume_is_absolute = false;
        data->save_volume = false;
    }

    if (!data->volume_writable)
        data->save_volume = false;

    if (pa_channel_map_valid(&volume_map))
        /* The original volume channel map may be different than the final
         * stream channel map, so remapping may be needed. */
        pa_cvolume_remap(&data->volume, &volume_map, &data->channel_map);

    if (!data->muted_is_set)
        data->muted = false;

    if (!(data->flags & PA_SINK_INPUT_VARIABLE_RATE) &&
        !pa_sample_spec_equal(&data->sample_spec, &data->sink->sample_spec)) {
        /* try to change sink format and rate. This is done before the FIXATE hook since
           module-suspend-on-idle can resume a sink */

        pa_log_info("Trying to change sample spec");
        pa_sink_reconfigure(data->sink, &data->sample_spec, pa_sink_input_new_data_is_passthrough(data));
    }

    if (pa_sink_input_new_data_is_passthrough(data) &&
        !pa_sample_spec_equal(&data->sample_spec, &data->sink->sample_spec)) {
        /* rate update failed, or other parts of sample spec didn't match */

        pa_log_debug("Could not update sink sample spec to match passthrough stream");
        return -PA_ERR_NOTSUPPORTED;
    }

    if (data->resample_method == PA_RESAMPLER_INVALID)
        data->resample_method = core->resample_method;

    pa_return_val_if_fail(data->resample_method < PA_RESAMPLER_MAX, -PA_ERR_INVALID);

    if ((r = pa_hook_fire(&core->hooks[PA_CORE_HOOK_SINK_INPUT_FIXATE], data)) < 0)
        return r;

    if ((data->flags & PA_SINK_INPUT_NO_CREATE_ON_SUSPEND) &&
        data->sink->state == PA_SINK_SUSPENDED) {
        pa_log_warn("Failed to create sink input: sink is suspended.");
        return -PA_ERR_BADSTATE;
    }

    if (pa_idxset_size(data->sink->inputs) >= PA_MAX_INPUTS_PER_SINK) {
        pa_log_warn("Failed to create sink input: too many inputs per sink.");
        return -PA_ERR_TOOLARGE;
    }

    if ((data->flags & PA_SINK_INPUT_VARIABLE_RATE) ||
        !pa_sample_spec_equal(&data->sample_spec, &data->sink->sample_spec) ||
        !pa_channel_map_equal(&data->channel_map, &data->sink->channel_map)) {

        /* Note: for passthrough content we need to adjust the output rate to that of the current sink-input */
        if (!pa_sink_input_new_data_is_passthrough(data)) /* no resampler for passthrough content */
            if (!(resampler = pa_resampler_new(
                          core->mempool,
                          &data->sample_spec, &data->channel_map,
                          &data->sink->sample_spec, &data->sink->channel_map,
                          core->lfe_crossover_freq,
                          data->resample_method,
                          ((data->flags & PA_SINK_INPUT_VARIABLE_RATE) ? PA_RESAMPLER_VARIABLE_RATE : 0) |
                          ((data->flags & PA_SINK_INPUT_NO_REMAP) ? PA_RESAMPLER_NO_REMAP : 0) |
                          (core->disable_remixing || (data->flags & PA_SINK_INPUT_NO_REMIX) ? PA_RESAMPLER_NO_REMIX : 0) |
                          (core->remixing_use_all_sink_channels ? 0 : PA_RESAMPLER_NO_FILL_SINK) |
                          (core->remixing_produce_lfe ? PA_RESAMPLER_PRODUCE_LFE : 0) |
                          (core->remixing_consume_lfe ? PA_RESAMPLER_CONSUME_LFE : 0)))) {
                pa_log_warn("Unsupported resampling operation.");
                return -PA_ERR_NOTSUPPORTED;
            }
    }

    i = pa_msgobject_new(pa_sink_input);
    i->parent.parent.free = sink_input_free;
    i->parent.process_msg = pa_sink_input_process_msg;

    i->core = core;
    i->state = PA_SINK_INPUT_INIT;
    i->flags = data->flags;
    i->proplist = pa_proplist_copy(data->proplist);
    i->driver = pa_xstrdup(pa_path_get_filename(data->driver));
    i->module = data->module;
    i->sink = data->sink;
    i->sink_requested_by_application = data->sink_requested_by_application;
    i->origin_sink = data->origin_sink;
    i->client = data->client;

    i->requested_resample_method = data->resample_method;
    i->actual_resample_method = resampler ? pa_resampler_get_method(resampler) : PA_RESAMPLER_INVALID;
    i->sample_spec = data->sample_spec;
    i->channel_map = data->channel_map;
    i->format = pa_format_info_copy(data->format);

    if (!data->volume_is_absolute && pa_sink_flat_volume_enabled(i->sink)) {
        pa_cvolume remapped;

        /* When the 'absolute' bool is not set then we'll treat the volume
         * as relative to the sink volume even in flat volume mode */
        remapped = data->sink->reference_volume;
        pa_cvolume_remap(&remapped, &data->sink->channel_map, &data->channel_map);
        pa_sw_cvolume_multiply(&i->volume, &data->volume, &remapped);
    } else
        i->volume = data->volume;

    i->volume_factor_items = data->volume_factor_items;
    data->volume_factor_items = NULL;
    volume_factor_from_hashmap(&i->volume_factor, i->volume_factor_items, i->sample_spec.channels);

    i->volume_factor_sink_items = data->volume_factor_sink_items;
    data->volume_factor_sink_items = NULL;
    volume_factor_from_hashmap(&i->volume_factor_sink, i->volume_factor_sink_items, i->sink->sample_spec.channels);

    i->real_ratio = i->reference_ratio = data->volume;
    pa_cvolume_reset(&i->soft_volume, i->sample_spec.channels);
    pa_cvolume_reset(&i->real_ratio, i->sample_spec.channels);
    i->volume_writable = data->volume_writable;
    i->save_volume = data->save_volume;
    i->preferred_sink = pa_xstrdup(data->preferred_sink);
    i->save_muted = data->save_muted;

    i->muted = data->muted;

    if (data->sync_base) {
        i->sync_next = data->sync_base->sync_next;
        i->sync_prev = data->sync_base;

        if (data->sync_base->sync_next)
            data->sync_base->sync_next->sync_prev = i;
        data->sync_base->sync_next = i;
    } else
        i->sync_next = i->sync_prev = NULL;

    i->direct_outputs = pa_idxset_new(NULL, NULL);

    reset_callbacks(i);
    i->userdata = NULL;

    i->thread_info.state = i->state;
    i->thread_info.attached = false;
    i->thread_info.sample_spec = i->sample_spec;
    i->thread_info.resampler = resampler;
    i->thread_info.soft_volume = i->soft_volume;
    i->thread_info.muted = i->muted;
    i->thread_info.requested_sink_latency = (pa_usec_t) -1;
    i->thread_info.rewrite_nbytes = 0;
    i->thread_info.rewrite_flush = false;
    i->thread_info.dont_rewind_render = false;
    i->thread_info.underrun_for = (uint64_t) -1;
    i->thread_info.underrun_for_sink = 0;
    i->thread_info.playing_for = 0;
    i->thread_info.direct_outputs = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    pa_assert_se(pa_idxset_put(core->sink_inputs, i, &i->index) == 0);
    pa_assert_se(pa_idxset_put(i->sink->inputs, pa_sink_input_ref(i), NULL) == 0);

    if (i->client)
        pa_assert_se(pa_idxset_put(i->client->sink_inputs, i, NULL) >= 0);

    memblockq_name = pa_sprintf_malloc("sink input render_memblockq [%u]", i->index);
    i->thread_info.render_memblockq = pa_memblockq_new(
            memblockq_name,
            0,
            MEMBLOCKQ_MAXLENGTH,
            0,
            &i->sink->sample_spec,
            0,
            1,
            0,
            &i->sink->silence);
    pa_xfree(memblockq_name);

    pt = pa_proplist_to_string_sep(i->proplist, "\n    ");
    pa_log_info("Created input %u \"%s\" on %s with sample spec %s and channel map %s\n    %s",
                i->index,
                pa_strnull(pa_proplist_gets(i->proplist, PA_PROP_MEDIA_NAME)),
                i->sink->name,
                pa_sample_spec_snprint(st, sizeof(st), &i->sample_spec),
                pa_channel_map_snprint(cm, sizeof(cm), &i->channel_map),
                pt);
    pa_xfree(pt);

    /* Don't forget to call pa_sink_input_put! */

    *_i = i;
    return 0;
}

/* Called from main context */
static void update_n_corked(pa_sink_input *i, pa_sink_input_state_t state) {
    pa_assert(i);
    pa_assert_ctl_context();

    if (!i->sink)
        return;

    if (i->state == PA_SINK_INPUT_CORKED && state != PA_SINK_INPUT_CORKED)
        pa_assert_se(i->sink->n_corked -- >= 1);
    else if (i->state != PA_SINK_INPUT_CORKED && state == PA_SINK_INPUT_CORKED)
        i->sink->n_corked++;
}

/* Called from main context */
static void sink_input_set_state(pa_sink_input *i, pa_sink_input_state_t state) {
    pa_sink_input *ssync;
    pa_assert(i);
    pa_assert_ctl_context();

    if (i->state == state)
        return;

    if (i->sink) {
        if (i->state == PA_SINK_INPUT_CORKED && state == PA_SINK_INPUT_RUNNING && pa_sink_used_by(i->sink) == 0 &&
            !pa_sample_spec_equal(&i->sample_spec, &i->sink->sample_spec)) {
            /* We were uncorked and the sink was not playing anything -- let's try
             * to update the sample format and rate to avoid resampling */
            pa_sink_reconfigure(i->sink, &i->sample_spec, pa_sink_input_is_passthrough(i));
        }

        pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i), PA_SINK_INPUT_MESSAGE_SET_STATE, PA_UINT_TO_PTR(state), 0, NULL) == 0);
    } else {
        /* If the sink is not valid, pa_sink_input_set_state_within_thread() must be called directly */

        pa_sink_input_set_state_within_thread(i, state);

        for (ssync = i->thread_info.sync_prev; ssync; ssync = ssync->thread_info.sync_prev)
            pa_sink_input_set_state_within_thread(ssync, state);

        for (ssync = i->thread_info.sync_next; ssync; ssync = ssync->thread_info.sync_next)
            pa_sink_input_set_state_within_thread(ssync, state);
    }

    update_n_corked(i, state);
    i->state = state;

    for (ssync = i->sync_prev; ssync; ssync = ssync->sync_prev) {
        update_n_corked(ssync, state);
        ssync->state = state;
    }
    for (ssync = i->sync_next; ssync; ssync = ssync->sync_next) {
        update_n_corked(ssync, state);
        ssync->state = state;
    }

    if (state != PA_SINK_INPUT_UNLINKED) {
        pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_STATE_CHANGED], i);

        for (ssync = i->sync_prev; ssync; ssync = ssync->sync_prev)
            pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_STATE_CHANGED], ssync);

        for (ssync = i->sync_next; ssync; ssync = ssync->sync_next)
            pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_STATE_CHANGED], ssync);

        if (PA_SINK_INPUT_IS_LINKED(state))
            pa_subscription_post(i->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_CHANGE, i->index);
    }

    if (i->sink)
        pa_sink_update_status(i->sink);
}

/* Called from main context */
void pa_sink_input_unlink(pa_sink_input *i) {
    bool linked;
    pa_source_output *o, PA_UNUSED *p = NULL;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();

    /* See pa_sink_unlink() for a couple of comments how this function
     * works */

    pa_sink_input_ref(i);

    linked = PA_SINK_INPUT_IS_LINKED(i->state);

    if (linked)
        pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK], i);

    if (i->sync_prev)
        i->sync_prev->sync_next = i->sync_next;
    if (i->sync_next)
        i->sync_next->sync_prev = i->sync_prev;

    i->sync_prev = i->sync_next = NULL;

    pa_idxset_remove_by_data(i->core->sink_inputs, i, NULL);

    if (i->sink)
        if (pa_idxset_remove_by_data(i->sink->inputs, i, NULL))
            pa_sink_input_unref(i);

    if (i->client)
        pa_idxset_remove_by_data(i->client->sink_inputs, i, NULL);

    while ((o = pa_idxset_first(i->direct_outputs, NULL))) {
        pa_assert(o != p);
        pa_source_output_kill(o);
        p = o;
    }

    update_n_corked(i, PA_SINK_INPUT_UNLINKED);
    i->state = PA_SINK_INPUT_UNLINKED;

    if (linked && i->sink) {
        if (pa_sink_input_is_passthrough(i))
            pa_sink_leave_passthrough(i->sink);

        /* We might need to update the sink's volume if we are in flat volume mode. */
        if (pa_sink_flat_volume_enabled(i->sink))
            pa_sink_set_volume(i->sink, NULL, false, false);

        if (i->sink->asyncmsgq)
            pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i->sink), PA_SINK_MESSAGE_REMOVE_INPUT, i, 0, NULL) == 0);
    }

    reset_callbacks(i);

    if (i->sink) {
        if (PA_SINK_IS_LINKED(i->sink->state))
            pa_sink_update_status(i->sink);

        i->sink = NULL;
    }

    if (linked) {
        pa_subscription_post(i->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_REMOVE, i->index);
        pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK_POST], i);
    }

    pa_core_maybe_vacuum(i->core);

    pa_sink_input_unref(i);
}

/* Called from main context */
static void sink_input_free(pa_object *o) {
    pa_sink_input* i = PA_SINK_INPUT(o);

    pa_assert(i);
    pa_assert_ctl_context();
    pa_assert(pa_sink_input_refcnt(i) == 0);
    pa_assert(!PA_SINK_INPUT_IS_LINKED(i->state));

    pa_log_info("Freeing input %u \"%s\"", i->index,
                i->proplist ? pa_strnull(pa_proplist_gets(i->proplist, PA_PROP_MEDIA_NAME)) : "");

    /* Side note: this function must be able to destruct properly any
     * kind of sink input in any state, even those which are
     * "half-moved" or are connected to sinks that have no asyncmsgq
     * and are hence half-destructed themselves! */

    if (i->thread_info.render_memblockq)
        pa_memblockq_free(i->thread_info.render_memblockq);

    if (i->thread_info.resampler)
        pa_resampler_free(i->thread_info.resampler);

    if (i->format)
        pa_format_info_free(i->format);

    if (i->proplist)
        pa_proplist_free(i->proplist);

    if (i->direct_outputs)
        pa_idxset_free(i->direct_outputs, NULL);

    if (i->thread_info.direct_outputs)
        pa_hashmap_free(i->thread_info.direct_outputs);

    if (i->volume_factor_items)
        pa_hashmap_free(i->volume_factor_items);

    if (i->volume_factor_sink_items)
        pa_hashmap_free(i->volume_factor_sink_items);

    if (i->preferred_sink)
        pa_xfree(i->preferred_sink);

    pa_xfree(i->driver);
    pa_xfree(i);
}

/* Called from main context */
void pa_sink_input_put(pa_sink_input *i) {
    pa_sink_input_state_t state;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();

    pa_assert(i->state == PA_SINK_INPUT_INIT);

    /* The following fields must be initialized properly */
    pa_assert(i->pop);
    pa_assert(i->process_rewind);
    pa_assert(i->kill);

    state = i->flags & PA_SINK_INPUT_START_CORKED ? PA_SINK_INPUT_CORKED : PA_SINK_INPUT_RUNNING;

    update_n_corked(i, state);
    i->state = state;

    /* We might need to update the sink's volume if we are in flat volume mode. */
    if (pa_sink_flat_volume_enabled(i->sink))
        pa_sink_set_volume(i->sink, NULL, false, i->save_volume);
    else {
        if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)) {
            pa_assert(pa_cvolume_is_norm(&i->volume));
            pa_assert(pa_cvolume_is_norm(&i->reference_ratio));
        }

        set_real_ratio(i, &i->volume);
    }

    if (pa_sink_input_is_passthrough(i))
        pa_sink_enter_passthrough(i->sink);

    i->thread_info.soft_volume = i->soft_volume;
    i->thread_info.muted = i->muted;

    pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i->sink), PA_SINK_MESSAGE_ADD_INPUT, i, 0, NULL) == 0);

    pa_subscription_post(i->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_NEW, i->index);
    pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_PUT], i);

    pa_sink_update_status(i->sink);
}

/* Called from main context */
void pa_sink_input_kill(pa_sink_input*i) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));

    i->kill(i);
}

/* Called from main context */
pa_usec_t pa_sink_input_get_latency(pa_sink_input *i, pa_usec_t *sink_latency) {
    pa_usec_t r[2] = { 0, 0 };

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));

    pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i), PA_SINK_INPUT_MESSAGE_GET_LATENCY, r, 0, NULL) == 0);

    if (i->get_latency)
        r[0] += i->get_latency(i);

    if (sink_latency)
        *sink_latency = r[1];

    return r[0];
}

/* Called from thread context */
void pa_sink_input_peek(pa_sink_input *i, size_t slength /* in sink bytes */, pa_memchunk *chunk, pa_cvolume *volume) {
    bool do_volume_adj_here, need_volume_factor_sink;
    bool volume_is_norm;
    size_t block_size_max_sink, block_size_max_sink_input;
    size_t ilength;
    size_t ilength_full;

    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->thread_info.state));
    pa_assert(pa_frame_aligned(slength, &i->sink->sample_spec));
    pa_assert(chunk);
    pa_assert(volume);

#ifdef SINK_INPUT_DEBUG
    pa_log_debug("peek");
#endif

    block_size_max_sink_input = i->thread_info.resampler ?
        pa_resampler_max_block_size(i->thread_info.resampler) :
        pa_frame_align(pa_mempool_block_size_max(i->core->mempool), &i->sample_spec);

    block_size_max_sink = pa_frame_align(pa_mempool_block_size_max(i->core->mempool), &i->sink->sample_spec);

    /* Default buffer size */
    if (slength <= 0)
        slength = pa_frame_align(CONVERT_BUFFER_LENGTH, &i->sink->sample_spec);

    if (slength > block_size_max_sink)
        slength = block_size_max_sink;

    if (i->thread_info.resampler) {
        ilength = pa_resampler_request(i->thread_info.resampler, slength);

        if (ilength <= 0)
            ilength = pa_frame_align(CONVERT_BUFFER_LENGTH, &i->sample_spec);
    } else
        ilength = slength;

    /* Length corresponding to slength (without limiting to
     * block_size_max_sink_input). */
    ilength_full = ilength;

    if (ilength > block_size_max_sink_input)
        ilength = block_size_max_sink_input;

    /* If the channel maps of the sink and this stream differ, we need
     * to adjust the volume *before* we resample. Otherwise we can do
     * it after and leave it for the sink code */

    do_volume_adj_here = !pa_channel_map_equal(&i->channel_map, &i->sink->channel_map);
    volume_is_norm = pa_cvolume_is_norm(&i->thread_info.soft_volume) && !i->thread_info.muted;
    need_volume_factor_sink = !pa_cvolume_is_norm(&i->volume_factor_sink);

    while (!pa_memblockq_is_readable(i->thread_info.render_memblockq)) {
        pa_memchunk tchunk;

        /* There's nothing in our render queue. We need to fill it up
         * with data from the implementor. */

        if (i->thread_info.state == PA_SINK_INPUT_CORKED ||
            i->pop(i, ilength, &tchunk) < 0) {

            /* OK, we're corked or the implementor didn't give us any
             * data, so let's just hand out silence */

            pa_memblockq_seek(i->thread_info.render_memblockq, (int64_t) slength, PA_SEEK_RELATIVE, true);
            i->thread_info.playing_for = 0;
            if (i->thread_info.underrun_for != (uint64_t) -1) {
                i->thread_info.underrun_for += ilength_full;
                i->thread_info.underrun_for_sink += slength;
            }
            break;
        }

        pa_assert(tchunk.length > 0);
        pa_assert(tchunk.memblock);

        i->thread_info.underrun_for = 0;
        i->thread_info.underrun_for_sink = 0;
        i->thread_info.playing_for += tchunk.length;

        while (tchunk.length > 0) {
            pa_memchunk wchunk;
            bool nvfs = need_volume_factor_sink;

            wchunk = tchunk;
            pa_memblock_ref(wchunk.memblock);

            if (wchunk.length > block_size_max_sink_input)
                wchunk.length = block_size_max_sink_input;

            /* It might be necessary to adjust the volume here */
            if (do_volume_adj_here && !volume_is_norm) {
                pa_memchunk_make_writable(&wchunk, 0);

                if (i->thread_info.muted) {
                    pa_silence_memchunk(&wchunk, &i->thread_info.sample_spec);
                    nvfs = false;

                } else if (!i->thread_info.resampler && nvfs) {
                    pa_cvolume v;

                    /* If we don't need a resampler we can merge the
                     * post and the pre volume adjustment into one */

                    pa_sw_cvolume_multiply(&v, &i->thread_info.soft_volume, &i->volume_factor_sink);
                    pa_volume_memchunk(&wchunk, &i->thread_info.sample_spec, &v);
                    nvfs = false;

                } else
                    pa_volume_memchunk(&wchunk, &i->thread_info.sample_spec, &i->thread_info.soft_volume);
            }

            if (!i->thread_info.resampler) {

                if (nvfs) {
                    pa_memchunk_make_writable(&wchunk, 0);
                    pa_volume_memchunk(&wchunk, &i->sink->sample_spec, &i->volume_factor_sink);
                }

                pa_memblockq_push_align(i->thread_info.render_memblockq, &wchunk);
            } else {
                pa_memchunk rchunk;
                pa_resampler_run(i->thread_info.resampler, &wchunk, &rchunk);

#ifdef SINK_INPUT_DEBUG
                pa_log_debug("pushing %lu", (unsigned long) rchunk.length);
#endif

                if (rchunk.memblock) {

                    if (nvfs) {
                        pa_memchunk_make_writable(&rchunk, 0);
                        pa_volume_memchunk(&rchunk, &i->sink->sample_spec, &i->volume_factor_sink);
                    }

                    pa_memblockq_push_align(i->thread_info.render_memblockq, &rchunk);
                    pa_memblock_unref(rchunk.memblock);
                }
            }

            pa_memblock_unref(wchunk.memblock);

            tchunk.index += wchunk.length;
            tchunk.length -= wchunk.length;
        }

        pa_memblock_unref(tchunk.memblock);
    }

    pa_assert_se(pa_memblockq_peek(i->thread_info.render_memblockq, chunk) >= 0);

    pa_assert(chunk->length > 0);
    pa_assert(chunk->memblock);

#ifdef SINK_INPUT_DEBUG
    pa_log_debug("peeking %lu", (unsigned long) chunk->length);
#endif

    if (chunk->length > block_size_max_sink)
        chunk->length = block_size_max_sink;

    /* Let's see if we had to apply the volume adjustment ourselves,
     * or if this can be done by the sink for us */

    if (do_volume_adj_here)
        /* We had different channel maps, so we already did the adjustment */
        pa_cvolume_reset(volume, i->sink->sample_spec.channels);
    else if (i->thread_info.muted)
        /* We've both the same channel map, so let's have the sink do the adjustment for us*/
        pa_cvolume_mute(volume, i->sink->sample_spec.channels);
    else
        *volume = i->thread_info.soft_volume;
}

/* Called from thread context */
void pa_sink_input_drop(pa_sink_input *i, size_t nbytes /* in sink sample spec */) {

    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->thread_info.state));
    pa_assert(pa_frame_aligned(nbytes, &i->sink->sample_spec));
    pa_assert(nbytes > 0);

#ifdef SINK_INPUT_DEBUG
    pa_log_debug("dropping %lu", (unsigned long) nbytes);
#endif

    pa_memblockq_drop(i->thread_info.render_memblockq, nbytes);
}

/* Called from thread context */
bool pa_sink_input_process_underrun(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);

    if (pa_memblockq_is_readable(i->thread_info.render_memblockq))
        return false;

    if (i->process_underrun && i->process_underrun(i)) {
        /* All valid data has been played back, so we can empty this queue. */
        pa_memblockq_silence(i->thread_info.render_memblockq);
        return true;
    }
    return false;
}

/* Called from thread context */
void pa_sink_input_process_rewind(pa_sink_input *i, size_t nbytes /* in sink sample spec */) {
    size_t lbq;
    bool called = false;

    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->thread_info.state));
    pa_assert(pa_frame_aligned(nbytes, &i->sink->sample_spec));

#ifdef SINK_INPUT_DEBUG
    pa_log_debug("rewind(%lu, %lu)", (unsigned long) nbytes, (unsigned long) i->thread_info.rewrite_nbytes);
#endif

    lbq = pa_memblockq_get_length(i->thread_info.render_memblockq);

    if (nbytes > 0 && !i->thread_info.dont_rewind_render) {
        pa_log_debug("Have to rewind %lu bytes on render memblockq.", (unsigned long) nbytes);
        pa_memblockq_rewind(i->thread_info.render_memblockq, nbytes);
    }

    if (i->thread_info.rewrite_nbytes == (size_t) -1) {

        /* We were asked to drop all buffered data, and rerequest new
         * data from implementor the next time peek() is called */

        pa_memblockq_flush_write(i->thread_info.render_memblockq, true);

    } else if (i->thread_info.rewrite_nbytes > 0) {
        size_t max_rewrite, amount;

        /* Calculate how much make sense to rewrite at most */
        max_rewrite = nbytes;
        if (nbytes > 0)
            max_rewrite += lbq;

        /* Transform into local domain */
        if (i->thread_info.resampler)
            max_rewrite = pa_resampler_request(i->thread_info.resampler, max_rewrite);

        /* Calculate how much of the rewinded data should actually be rewritten */
        amount = PA_MIN(i->thread_info.rewrite_nbytes, max_rewrite);

        if (amount > 0) {
            pa_log_debug("Have to rewind %lu bytes on implementor.", (unsigned long) amount);

            /* Tell the implementor */
            if (i->process_rewind)
                i->process_rewind(i, amount);
            called = true;

            /* Convert back to sink domain */
            if (i->thread_info.resampler)
                amount = pa_resampler_result(i->thread_info.resampler, amount);

            if (amount > 0)
                /* Ok, now update the write pointer */
                pa_memblockq_seek(i->thread_info.render_memblockq, - ((int64_t) amount), PA_SEEK_RELATIVE, true);

            if (i->thread_info.rewrite_flush)
                pa_memblockq_silence(i->thread_info.render_memblockq);

            /* And rewind the resampler */
            if (i->thread_info.resampler)
                pa_resampler_rewind(i->thread_info.resampler, amount);
        }
    }

    if (!called)
        if (i->process_rewind)
            i->process_rewind(i, 0);

    i->thread_info.rewrite_nbytes = 0;
    i->thread_info.rewrite_flush = false;
    i->thread_info.dont_rewind_render = false;
}

/* Called from thread context */
size_t pa_sink_input_get_max_rewind(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);

    return i->thread_info.resampler ? pa_resampler_request(i->thread_info.resampler, i->sink->thread_info.max_rewind) : i->sink->thread_info.max_rewind;
}

/* Called from thread context */
size_t pa_sink_input_get_max_request(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);

    /* We're not verifying the status here, to allow this to be called
     * in the state change handler between _INIT and _RUNNING */

    return i->thread_info.resampler ? pa_resampler_request(i->thread_info.resampler, i->sink->thread_info.max_request) : i->sink->thread_info.max_request;
}

/* Called from thread context */
void pa_sink_input_update_max_rewind(pa_sink_input *i, size_t nbytes  /* in the sink's sample spec */) {
    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->thread_info.state));
    pa_assert(pa_frame_aligned(nbytes, &i->sink->sample_spec));

    pa_memblockq_set_maxrewind(i->thread_info.render_memblockq, nbytes);

    if (i->update_max_rewind)
        i->update_max_rewind(i, i->thread_info.resampler ? pa_resampler_request(i->thread_info.resampler, nbytes) : nbytes);
}

/* Called from thread context */
void pa_sink_input_update_max_request(pa_sink_input *i, size_t nbytes  /* in the sink's sample spec */) {
    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->thread_info.state));
    pa_assert(pa_frame_aligned(nbytes, &i->sink->sample_spec));

    if (i->update_max_request)
        i->update_max_request(i, i->thread_info.resampler ? pa_resampler_request(i->thread_info.resampler, nbytes) : nbytes);
}

/* Called from thread context */
pa_usec_t pa_sink_input_set_requested_latency_within_thread(pa_sink_input *i, pa_usec_t usec) {
    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);

    if (!(i->sink->flags & PA_SINK_DYNAMIC_LATENCY))
        usec = i->sink->thread_info.fixed_latency;

    if (usec != (pa_usec_t) -1)
        usec = PA_CLAMP(usec, i->sink->thread_info.min_latency, i->sink->thread_info.max_latency);

    i->thread_info.requested_sink_latency = usec;
    pa_sink_invalidate_requested_latency(i->sink, true);

    return usec;
}

/* Called from main context */
pa_usec_t pa_sink_input_set_requested_latency(pa_sink_input *i, pa_usec_t usec) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();

    if (PA_SINK_INPUT_IS_LINKED(i->state) && i->sink) {
        pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i), PA_SINK_INPUT_MESSAGE_SET_REQUESTED_LATENCY, &usec, 0, NULL) == 0);
        return usec;
    }

    /* If this sink input is not realized yet or we are being moved,
     * we have to touch the thread info data directly */

    if (i->sink) {
        if (!(i->sink->flags & PA_SINK_DYNAMIC_LATENCY))
            usec = pa_sink_get_fixed_latency(i->sink);

        if (usec != (pa_usec_t) -1) {
            pa_usec_t min_latency, max_latency;
            pa_sink_get_latency_range(i->sink, &min_latency, &max_latency);
            usec = PA_CLAMP(usec, min_latency, max_latency);
        }
    }

    i->thread_info.requested_sink_latency = usec;

    return usec;
}

/* Called from main context */
pa_usec_t pa_sink_input_get_requested_latency(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();

    if (PA_SINK_INPUT_IS_LINKED(i->state) && i->sink) {
        pa_usec_t usec = 0;
        pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i), PA_SINK_INPUT_MESSAGE_GET_REQUESTED_LATENCY, &usec, 0, NULL) == 0);
        return usec;
    }

    /* If this sink input is not realized yet or we are being moved,
     * we have to touch the thread info data directly */

    return i->thread_info.requested_sink_latency;
}

/* Called from main context */
void pa_sink_input_set_volume(pa_sink_input *i, const pa_cvolume *volume, bool save, bool absolute) {
    pa_cvolume v;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_assert(volume);
    pa_assert(pa_cvolume_valid(volume));
    pa_assert(volume->channels == 1 || pa_cvolume_compatible(volume, &i->sample_spec));
    pa_assert(i->volume_writable);

    if (!absolute && pa_sink_flat_volume_enabled(i->sink)) {
        v = i->sink->reference_volume;
        pa_cvolume_remap(&v, &i->sink->channel_map, &i->channel_map);

        if (pa_cvolume_compatible(volume, &i->sample_spec))
            volume = pa_sw_cvolume_multiply(&v, &v, volume);
        else
            volume = pa_sw_cvolume_multiply_scalar(&v, &v, pa_cvolume_max(volume));
    } else {
        if (!pa_cvolume_compatible(volume, &i->sample_spec)) {
            v = i->volume;
            volume = pa_cvolume_scale(&v, pa_cvolume_max(volume));
        }
    }

    if (pa_cvolume_equal(volume, &i->volume)) {
        i->save_volume = i->save_volume || save;
        return;
    }

    pa_sink_input_set_volume_direct(i, volume);
    i->save_volume = save;

    if (pa_sink_flat_volume_enabled(i->sink)) {
        /* We are in flat volume mode, so let's update all sink input
         * volumes and update the flat volume of the sink */

        pa_sink_set_volume(i->sink, NULL, true, save);

    } else {
        /* OK, we are in normal volume mode. The volume only affects
         * ourselves */
        set_real_ratio(i, volume);
        pa_sink_input_set_reference_ratio(i, &i->volume);

        /* Copy the new soft_volume to the thread_info struct */
        pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i), PA_SINK_INPUT_MESSAGE_SET_SOFT_VOLUME, NULL, 0, NULL) == 0);
    }
}

void pa_sink_input_add_volume_factor(pa_sink_input *i, const char *key, const pa_cvolume *volume_factor) {
    struct volume_factor_entry *v;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_assert(volume_factor);
    pa_assert(key);
    pa_assert(pa_cvolume_valid(volume_factor));
    pa_assert(volume_factor->channels == 1 || pa_cvolume_compatible(volume_factor, &i->sample_spec));

    v = volume_factor_entry_new(key, volume_factor);
    if (!pa_cvolume_compatible(volume_factor, &i->sample_spec))
        pa_cvolume_set(&v->volume, i->sample_spec.channels, volume_factor->values[0]);

    pa_assert_se(pa_hashmap_put(i->volume_factor_items, v->key, v) >= 0);
    if (pa_hashmap_size(i->volume_factor_items) == 1)
        i->volume_factor = v->volume;
    else
        pa_sw_cvolume_multiply(&i->volume_factor, &i->volume_factor, &v->volume);

    pa_sw_cvolume_multiply(&i->soft_volume, &i->real_ratio, &i->volume_factor);

    /* Copy the new soft_volume to the thread_info struct */
    pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i), PA_SINK_INPUT_MESSAGE_SET_SOFT_VOLUME, NULL, 0, NULL) == 0);
}

/* Returns 0 if an entry was removed and -1 if no entry for the given key was
 * found. */
int pa_sink_input_remove_volume_factor(pa_sink_input *i, const char *key) {
    struct volume_factor_entry *v;

    pa_sink_input_assert_ref(i);
    pa_assert(key);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));

    if (pa_hashmap_remove_and_free(i->volume_factor_items, key) < 0)
        return -1;

    switch (pa_hashmap_size(i->volume_factor_items)) {
        case 0:
            pa_cvolume_reset(&i->volume_factor, i->sample_spec.channels);
            break;
        case 1:
            v = pa_hashmap_first(i->volume_factor_items);
            i->volume_factor = v->volume;
            break;
        default:
            volume_factor_from_hashmap(&i->volume_factor, i->volume_factor_items, i->volume_factor.channels);
    }

    pa_sw_cvolume_multiply(&i->soft_volume, &i->real_ratio, &i->volume_factor);

    /* Copy the new soft_volume to the thread_info struct */
    pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i), PA_SINK_INPUT_MESSAGE_SET_SOFT_VOLUME, NULL, 0, NULL) == 0);

    return 0;
}

/* Called from main context */
static void set_real_ratio(pa_sink_input *i, const pa_cvolume *v) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_assert(!v || pa_cvolume_compatible(v, &i->sample_spec));

    /* This basically calculates:
     *
     * i->real_ratio := v
     * i->soft_volume := i->real_ratio * i->volume_factor */

    if (v)
        i->real_ratio = *v;
    else
        pa_cvolume_reset(&i->real_ratio, i->sample_spec.channels);

    pa_sw_cvolume_multiply(&i->soft_volume, &i->real_ratio, &i->volume_factor);
    /* We don't copy the data to the thread_info data. That's left for someone else to do */
}

/* Called from main or I/O context */
bool pa_sink_input_is_passthrough(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);

    if (PA_UNLIKELY(!pa_format_info_is_pcm(i->format)))
        return true;

    if (PA_UNLIKELY(i->flags & PA_SINK_INPUT_PASSTHROUGH))
        return true;

    return false;
}

/* Called from main context */
bool pa_sink_input_is_volume_readable(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();

    return !pa_sink_input_is_passthrough(i);
}

/* Called from main context */
pa_cvolume *pa_sink_input_get_volume(pa_sink_input *i, pa_cvolume *volume, bool absolute) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_assert(pa_sink_input_is_volume_readable(i));

    if (absolute || !pa_sink_flat_volume_enabled(i->sink))
        *volume = i->volume;
    else
        *volume = i->reference_ratio;

    return volume;
}

/* Called from main context */
void pa_sink_input_set_mute(pa_sink_input *i, bool mute, bool save) {
    bool old_mute;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));

    old_mute = i->muted;

    if (mute == old_mute) {
        i->save_muted |= save;
        return;
    }

    i->muted = mute;
    pa_log_debug("The mute of sink input %u changed from %s to %s.", i->index, pa_yes_no(old_mute), pa_yes_no(mute));

    i->save_muted = save;

    pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i), PA_SINK_INPUT_MESSAGE_SET_SOFT_MUTE, NULL, 0, NULL) == 0);

    /* The mute status changed, let's tell people so */
    if (i->mute_changed)
        i->mute_changed(i);

    pa_subscription_post(i->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_CHANGE, i->index);
    pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_MUTE_CHANGED], i);
}

void pa_sink_input_set_property(pa_sink_input *i, const char *key, const char *value) {
    char *old_value = NULL;
    const char *new_value;

    pa_assert(i);
    pa_assert(key);

    if (pa_proplist_contains(i->proplist, key)) {
        old_value = pa_xstrdup(pa_proplist_gets(i->proplist, key));
        if (value && old_value && pa_streq(value, old_value))
            goto finish;

        if (!old_value)
            old_value = pa_xstrdup("(data)");
    } else {
        if (!value)
            goto finish;

        old_value = pa_xstrdup("(unset)");
    }

    if (value) {
        pa_proplist_sets(i->proplist, key, value);
        new_value = value;
    } else {
        pa_proplist_unset(i->proplist, key);
        new_value = "(unset)";
    }

    if (PA_SINK_INPUT_IS_LINKED(i->state)) {
        pa_log_debug("Sink input %u: proplist[%s]: %s -> %s", i->index, key, old_value, new_value);
        pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_PROPLIST_CHANGED], i);
        pa_subscription_post(i->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT | PA_SUBSCRIPTION_EVENT_CHANGE, i->index);
    }

finish:
    pa_xfree(old_value);
}

void pa_sink_input_set_property_arbitrary(pa_sink_input *i, const char *key, const uint8_t *value, size_t nbytes) {
    const uint8_t *old_value;
    size_t old_nbytes;
    const char *old_value_str;
    const char *new_value_str;

    pa_assert(i);
    pa_assert(key);

    if (pa_proplist_get(i->proplist, key, (const void **) &old_value, &old_nbytes) >= 0) {
        if (value && nbytes == old_nbytes && !memcmp(value, old_value, nbytes))
            return;

        old_value_str = "(data)";

    } else {
        if (!value)
            return;

        old_value_str = "(unset)";
    }

    if (value) {
        pa_proplist_set(i->proplist, key, value, nbytes);
        new_value_str = "(data)";
    } else {
        pa_proplist_unset(i->proplist, key);
        new_value_str = "(unset)";
    }

    if (PA_SINK_INPUT_IS_LINKED(i->state)) {
        pa_log_debug("Sink input %u: proplist[%s]: %s -> %s", i->index, key, old_value_str, new_value_str);
        pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_PROPLIST_CHANGED], i);
        pa_subscription_post(i->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT | PA_SUBSCRIPTION_EVENT_CHANGE, i->index);
    }
}

/* Called from main thread */
void pa_sink_input_update_proplist(pa_sink_input *i, pa_update_mode_t mode, pa_proplist *p) {
    void *state;
    const char *key;
    const uint8_t *value;
    size_t nbytes;

    pa_sink_input_assert_ref(i);
    pa_assert(p);
    pa_assert_ctl_context();

    switch (mode) {
        case PA_UPDATE_SET:
            /* Delete everything that is not in p. */
            for (state = NULL; (key = pa_proplist_iterate(i->proplist, &state));) {
                if (!pa_proplist_contains(p, key))
                    pa_sink_input_set_property(i, key, NULL);
            }

            /* Fall through. */
        case PA_UPDATE_REPLACE:
            for (state = NULL; (key = pa_proplist_iterate(p, &state));) {
                pa_proplist_get(p, key, (const void **) &value, &nbytes);
                pa_sink_input_set_property_arbitrary(i, key, value, nbytes);
            }

            break;
        case PA_UPDATE_MERGE:
            for (state = NULL; (key = pa_proplist_iterate(p, &state));) {
                if (pa_proplist_contains(i->proplist, key))
                    continue;

                pa_proplist_get(p, key, (const void **) &value, &nbytes);
                pa_sink_input_set_property_arbitrary(i, key, value, nbytes);
            }

            break;
    }
}

/* Called from main context */
void pa_sink_input_cork(pa_sink_input *i, bool b) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));

    sink_input_set_state(i, b ? PA_SINK_INPUT_CORKED : PA_SINK_INPUT_RUNNING);
}

/* Called from main context */
int pa_sink_input_set_rate(pa_sink_input *i, uint32_t rate) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_return_val_if_fail(i->thread_info.resampler, -PA_ERR_BADSTATE);

    if (i->sample_spec.rate == rate)
        return 0;

    i->sample_spec.rate = rate;

    if (i->sink)
        pa_asyncmsgq_post(i->sink->asyncmsgq, PA_MSGOBJECT(i), PA_SINK_INPUT_MESSAGE_SET_RATE, PA_UINT_TO_PTR(rate), 0, NULL, NULL);
    else {
        i->thread_info.sample_spec.rate = rate;
        pa_resampler_set_input_rate(i->thread_info.resampler, rate);
    }

    pa_subscription_post(i->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_CHANGE, i->index);
    return 0;
}

/* Called from main context */
pa_resample_method_t pa_sink_input_get_resample_method(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();

    return i->actual_resample_method;
}

/* Called from main context */
bool pa_sink_input_may_move(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));

    if (i->flags & PA_SINK_INPUT_DONT_MOVE)
        return false;

    if (i->sync_next || i->sync_prev) {
        pa_log_warn("Moving synchronized streams not supported.");
        return false;
    }

    return true;
}

static bool find_filter_sink_input(pa_sink_input *target, pa_sink *s) {
    unsigned PA_UNUSED i = 0;
    while (s && s->input_to_master) {
        if (s->input_to_master == target)
            return true;
        s = s->input_to_master->sink;
        pa_assert(i++ < 100);
    }
    return false;
}

static bool is_filter_sink_moving(pa_sink_input *i) {
    pa_sink *sink = i->sink;

    if (!sink)
        return false;

    while (sink->input_to_master) {
        sink = sink->input_to_master->sink;

        if (!sink)
            return true;
    }

    return false;
}

/* Called from main context */
bool pa_sink_input_may_move_to(pa_sink_input *i, pa_sink *dest) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_sink_assert_ref(dest);

    if (dest == i->sink)
        return true;

    if (dest->unlink_requested)
        return false;

    if (!pa_sink_input_may_move(i))
        return false;

    /* Make sure we're not creating a filter sink cycle */
    if (find_filter_sink_input(i, dest)) {
        pa_log_debug("Can't connect input to %s, as that would create a cycle.", dest->name);
        return false;
    }

    /* If this sink input is connected to a filter sink that itself is moving,
     * then don't allow the move. Moving requires sending a message to the IO
     * thread of the old sink, and if the old sink is a filter sink that is
     * moving, there's no IO thread associated to the old sink. */
    if (is_filter_sink_moving(i)) {
        pa_log_debug("Can't move input from filter sink %s, because the filter sink itself is currently moving.",
                     i->sink->name);
        return false;
    }

    if (pa_idxset_size(dest->inputs) >= PA_MAX_INPUTS_PER_SINK) {
        pa_log_warn("Failed to move sink input: too many inputs per sink.");
        return false;
    }

    if (check_passthrough_connection(pa_sink_input_is_passthrough(i), dest) < 0)
        return false;

    if (i->may_move_to)
        if (!i->may_move_to(i, dest))
            return false;

    return true;
}

/* Called from main context */
int pa_sink_input_start_move(pa_sink_input *i) {
    pa_source_output *o, PA_UNUSED *p = NULL;
    struct volume_factor_entry *v;
    void *state = NULL;
    int r;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_assert(i->sink);

    if (!pa_sink_input_may_move(i))
        return -PA_ERR_NOTSUPPORTED;

    if ((r = pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_START], i)) < 0)
        return r;

    pa_log_debug("Starting to move sink input %u from '%s'", (unsigned) i->index, i->sink->name);

    /* Kill directly connected outputs */
    while ((o = pa_idxset_first(i->direct_outputs, NULL))) {
        pa_assert(o != p);
        pa_source_output_kill(o);
        p = o;
    }
    pa_assert(pa_idxset_isempty(i->direct_outputs));

    pa_idxset_remove_by_data(i->sink->inputs, i, NULL);

    if (i->state == PA_SINK_INPUT_CORKED)
        pa_assert_se(i->sink->n_corked-- >= 1);

    if (pa_sink_input_is_passthrough(i))
        pa_sink_leave_passthrough(i->sink);

    if (pa_sink_flat_volume_enabled(i->sink))
        /* We might need to update the sink's volume if we are in flat
         * volume mode. */
        pa_sink_set_volume(i->sink, NULL, false, false);

    pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i->sink), PA_SINK_MESSAGE_START_MOVE, i, 0, NULL) == 0);

    pa_sink_update_status(i->sink);

    PA_HASHMAP_FOREACH(v, i->volume_factor_sink_items, state)
        pa_cvolume_remap(&v->volume, &i->sink->channel_map, &i->channel_map);

    pa_cvolume_remap(&i->volume_factor_sink, &i->sink->channel_map, &i->channel_map);

    i->sink = NULL;
    i->sink_requested_by_application = false;

    pa_sink_input_unref(i);

    return 0;
}

/* Called from main context. If i has an origin sink that uses volume sharing,
 * then also the origin sink and all streams connected to it need to update
 * their volume - this function does all that by using recursion. */
static void update_volume_due_to_moving(pa_sink_input *i, pa_sink *dest) {
    pa_cvolume new_volume;

    pa_assert(i);
    pa_assert(dest);
    pa_assert(i->sink); /* The destination sink should already be set. */

    if (i->origin_sink && (i->origin_sink->flags & PA_SINK_SHARE_VOLUME_WITH_MASTER)) {
        pa_sink *root_sink = pa_sink_get_master(i->sink);
        pa_sink_input *origin_sink_input;
        uint32_t idx;

        if (PA_UNLIKELY(!root_sink))
            return;

        if (pa_sink_flat_volume_enabled(i->sink)) {
            /* Ok, so the origin sink uses volume sharing, and flat volume is
             * enabled. The volume will have to be updated as follows:
             *
             *     i->volume := i->sink->real_volume
             *         (handled later by pa_sink_set_volume)
             *     i->reference_ratio := i->volume / i->sink->reference_volume
             *         (handled later by pa_sink_set_volume)
             *     i->real_ratio stays unchanged
             *         (streams whose origin sink uses volume sharing should
             *          always have real_ratio of 0 dB)
             *     i->soft_volume stays unchanged
             *         (streams whose origin sink uses volume sharing should
             *          always have volume_factor as soft_volume, so no change
             *          should be needed) */

            pa_assert(pa_cvolume_is_norm(&i->real_ratio));
            pa_assert(pa_cvolume_equal(&i->soft_volume, &i->volume_factor));

            /* Notifications will be sent by pa_sink_set_volume(). */

        } else {
            /* Ok, so the origin sink uses volume sharing, and flat volume is
             * disabled. The volume will have to be updated as follows:
             *
             *     i->volume := 0 dB
             *     i->reference_ratio := 0 dB
             *     i->real_ratio stays unchanged
             *         (streams whose origin sink uses volume sharing should
             *          always have real_ratio of 0 dB)
             *     i->soft_volume stays unchanged
             *         (streams whose origin sink uses volume sharing should
             *          always have volume_factor as soft_volume, so no change
             *          should be needed) */

            pa_cvolume_reset(&new_volume, i->volume.channels);
            pa_sink_input_set_volume_direct(i, &new_volume);
            pa_sink_input_set_reference_ratio(i, &new_volume);
            pa_assert(pa_cvolume_is_norm(&i->real_ratio));
            pa_assert(pa_cvolume_equal(&i->soft_volume, &i->volume_factor));
        }

        /* Additionally, the origin sink volume needs updating:
         *
         *     i->origin_sink->reference_volume := root_sink->reference_volume
         *     i->origin_sink->real_volume := root_sink->real_volume
         *     i->origin_sink->soft_volume stays unchanged
         *         (sinks that use volume sharing should always have
         *          soft_volume of 0 dB) */

        new_volume = root_sink->reference_volume;
        pa_cvolume_remap(&new_volume, &root_sink->channel_map, &i->origin_sink->channel_map);
        pa_sink_set_reference_volume_direct(i->origin_sink, &new_volume);

        i->origin_sink->real_volume = root_sink->real_volume;
        pa_cvolume_remap(&i->origin_sink->real_volume, &root_sink->channel_map, &i->origin_sink->channel_map);

        pa_assert(pa_cvolume_is_norm(&i->origin_sink->soft_volume));

        /* If you wonder whether i->origin_sink->set_volume() should be called
         * somewhere, that's not the case, because sinks that use volume
         * sharing shouldn't have any internal volume that set_volume() would
         * update. If you wonder whether the thread_info variables should be
         * synced, yes, they should, and it's done by the
         * PA_SINK_MESSAGE_FINISH_MOVE message handler. */

        /* Recursively update origin sink inputs. */
        PA_IDXSET_FOREACH(origin_sink_input, i->origin_sink->inputs, idx)
            update_volume_due_to_moving(origin_sink_input, dest);

    } else {
        if (pa_sink_flat_volume_enabled(i->sink)) {
            /* Ok, so this is a regular stream, and flat volume is enabled. The
             * volume will have to be updated as follows:
             *
             *     i->volume := i->reference_ratio * i->sink->reference_volume
             *     i->reference_ratio stays unchanged
             *     i->real_ratio := i->volume / i->sink->real_volume
             *         (handled later by pa_sink_set_volume)
             *     i->soft_volume := i->real_ratio * i->volume_factor
             *         (handled later by pa_sink_set_volume) */

            new_volume = i->sink->reference_volume;
            pa_cvolume_remap(&new_volume, &i->sink->channel_map, &i->channel_map);
            pa_sw_cvolume_multiply(&new_volume, &new_volume, &i->reference_ratio);
            pa_sink_input_set_volume_direct(i, &new_volume);

        } else {
            /* Ok, so this is a regular stream, and flat volume is disabled.
             * The volume will have to be updated as follows:
             *
             *     i->volume := i->reference_ratio
             *     i->reference_ratio stays unchanged
             *     i->real_ratio := i->reference_ratio
             *     i->soft_volume := i->real_ratio * i->volume_factor */

            pa_sink_input_set_volume_direct(i, &i->reference_ratio);
            i->real_ratio = i->reference_ratio;
            pa_sw_cvolume_multiply(&i->soft_volume, &i->real_ratio, &i->volume_factor);
        }
    }

    /* If i->sink == dest, then recursion has finished, and we can finally call
     * pa_sink_set_volume(), which will do the rest of the updates. */
    if ((i->sink == dest) && pa_sink_flat_volume_enabled(i->sink))
        pa_sink_set_volume(i->sink, NULL, false, i->save_volume);
}

/* Called from main context */
int pa_sink_input_finish_move(pa_sink_input *i, pa_sink *dest, bool save) {
    struct volume_factor_entry *v;
    void *state = NULL;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_assert(!i->sink);
    pa_sink_assert_ref(dest);

    if (!pa_sink_input_may_move_to(i, dest))
        return -PA_ERR_NOTSUPPORTED;

    if (pa_sink_input_is_passthrough(i) && !pa_sink_check_format(dest, i->format)) {
        pa_proplist *p = pa_proplist_new();
        pa_log_debug("New sink doesn't support stream format, sending format-changed and killing");
        /* Tell the client what device we want to be on if it is going to
         * reconnect */
        pa_proplist_sets(p, "device", dest->name);
        pa_sink_input_send_event(i, PA_STREAM_EVENT_FORMAT_LOST, p);
        pa_proplist_free(p);
        return -PA_ERR_NOTSUPPORTED;
    }

    if (!(i->flags & PA_SINK_INPUT_VARIABLE_RATE) &&
        !pa_sample_spec_equal(&i->sample_spec, &dest->sample_spec)) {
        /* try to change dest sink format and rate if possible without glitches.
           module-suspend-on-idle resumes destination sink with
           SINK_INPUT_MOVE_FINISH hook */

        pa_log_info("Trying to change sample spec");
        pa_sink_reconfigure(dest, &i->sample_spec, pa_sink_input_is_passthrough(i));
    }

    if (i->moving)
        i->moving(i, dest);

    i->sink = dest;
    /* save == true, means user is calling the move_to() and want to
       save the preferred_sink */
    if (save) {
        pa_xfree(i->preferred_sink);
        if (dest == dest->core->default_sink)
            i->preferred_sink = NULL;
        else
            i->preferred_sink = pa_xstrdup(dest->name);
    }

    pa_idxset_put(dest->inputs, pa_sink_input_ref(i), NULL);

    PA_HASHMAP_FOREACH(v, i->volume_factor_sink_items, state)
        pa_cvolume_remap(&v->volume, &i->channel_map, &i->sink->channel_map);

    pa_cvolume_remap(&i->volume_factor_sink, &i->channel_map, &i->sink->channel_map);

    if (i->state == PA_SINK_INPUT_CORKED)
        i->sink->n_corked++;

    pa_sink_input_update_resampler(i);

    pa_sink_update_status(dest);

    update_volume_due_to_moving(i, dest);

    if (pa_sink_input_is_passthrough(i))
        pa_sink_enter_passthrough(i->sink);

    pa_assert_se(pa_asyncmsgq_send(i->sink->asyncmsgq, PA_MSGOBJECT(i->sink), PA_SINK_MESSAGE_FINISH_MOVE, i, 0, NULL) == 0);

    pa_log_debug("Successfully moved sink input %i to %s.", i->index, dest->name);

    /* Notify everyone */
    pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_FINISH], i);
    pa_subscription_post(i->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_CHANGE, i->index);

    return 0;
}

/* Called from main context */
void pa_sink_input_fail_move(pa_sink_input *i) {

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_assert(!i->sink);

    /* Check if someone wants this sink input? */
    if (pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_FAIL], i) == PA_HOOK_STOP)
        return;

    /* Can we move the sink input to the default sink? */
    if (i->core->rescue_streams && pa_sink_input_may_move_to(i, i->core->default_sink)) {
        if (pa_sink_input_finish_move(i, i->core->default_sink, false) >= 0)
            return;
    }

    if (i->moving)
        i->moving(i, NULL);

    pa_sink_input_kill(i);
}

/* Called from main context */
int pa_sink_input_move_to(pa_sink_input *i, pa_sink *dest, bool save) {
    int r;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));
    pa_assert(i->sink);
    pa_sink_assert_ref(dest);

    if (dest == i->sink)
        return 0;

    if (!pa_sink_input_may_move_to(i, dest))
        return -PA_ERR_NOTSUPPORTED;

    pa_sink_input_ref(i);

    if ((r = pa_sink_input_start_move(i)) < 0) {
        pa_sink_input_unref(i);
        return r;
    }

    if ((r = pa_sink_input_finish_move(i, dest, save)) < 0) {
        pa_sink_input_fail_move(i);
        pa_sink_input_unref(i);
        return r;
    }

    pa_sink_input_unref(i);

    return 0;
}

/* Called from IO thread context except when cork() is called without a valid sink. */
void pa_sink_input_set_state_within_thread(pa_sink_input *i, pa_sink_input_state_t state) {
    bool corking, uncorking;

    pa_sink_input_assert_ref(i);

    if (state == i->thread_info.state)
        return;

    corking = state == PA_SINK_INPUT_CORKED && i->thread_info.state == PA_SINK_INPUT_RUNNING;
    uncorking = i->thread_info.state == PA_SINK_INPUT_CORKED && state == PA_SINK_INPUT_RUNNING;

    if (i->state_change)
        i->state_change(i, state);

    if (corking) {

        pa_log_debug("Requesting rewind due to corking");

        /* This will tell the implementing sink input driver to rewind
         * so that the unplayed already mixed data is not lost */
        if (i->sink)
            pa_sink_input_request_rewind(i, 0, true, true, false);

        /* Set the corked state *after* requesting rewind */
        i->thread_info.state = state;

    } else if (uncorking) {

        pa_log_debug("Requesting rewind due to uncorking");

        i->thread_info.underrun_for = (uint64_t) -1;
        i->thread_info.underrun_for_sink = 0;
        i->thread_info.playing_for = 0;

        /* Set the uncorked state *before* requesting rewind */
        i->thread_info.state = state;

        /* OK, we're being uncorked. Make sure we're not rewound when
         * the hw buffer is remixed and request a remix. */
        if (i->sink)
            pa_sink_input_request_rewind(i, 0, false, true, true);
    } else
        /* We may not be corking or uncorking, but we still need to set the state. */
        i->thread_info.state = state;
}

/* Called from thread context, except when it is not. */
int pa_sink_input_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk) {
    pa_sink_input *i = PA_SINK_INPUT(o);
    pa_sink_input_assert_ref(i);

    switch (code) {

        case PA_SINK_INPUT_MESSAGE_SET_SOFT_VOLUME:
            if (!pa_cvolume_equal(&i->thread_info.soft_volume, &i->soft_volume)) {
                i->thread_info.soft_volume = i->soft_volume;
                pa_sink_input_request_rewind(i, 0, true, false, false);
            }
            return 0;

        case PA_SINK_INPUT_MESSAGE_SET_SOFT_MUTE:
            if (i->thread_info.muted != i->muted) {
                i->thread_info.muted = i->muted;
                pa_sink_input_request_rewind(i, 0, true, false, false);
            }
            return 0;

        case PA_SINK_INPUT_MESSAGE_GET_LATENCY: {
            pa_usec_t *r = userdata;

            r[0] += pa_bytes_to_usec(pa_memblockq_get_length(i->thread_info.render_memblockq), &i->sink->sample_spec);
            r[1] += pa_sink_get_latency_within_thread(i->sink, false);

            return 0;
        }

        case PA_SINK_INPUT_MESSAGE_SET_RATE:

            i->thread_info.sample_spec.rate = PA_PTR_TO_UINT(userdata);
            pa_resampler_set_input_rate(i->thread_info.resampler, PA_PTR_TO_UINT(userdata));

            return 0;

        case PA_SINK_INPUT_MESSAGE_SET_STATE: {
            pa_sink_input *ssync;

            pa_sink_input_set_state_within_thread(i, PA_PTR_TO_UINT(userdata));

            for (ssync = i->thread_info.sync_prev; ssync; ssync = ssync->thread_info.sync_prev)
                pa_sink_input_set_state_within_thread(ssync, PA_PTR_TO_UINT(userdata));

            for (ssync = i->thread_info.sync_next; ssync; ssync = ssync->thread_info.sync_next)
                pa_sink_input_set_state_within_thread(ssync, PA_PTR_TO_UINT(userdata));

            return 0;
        }

        case PA_SINK_INPUT_MESSAGE_SET_REQUESTED_LATENCY: {
            pa_usec_t *usec = userdata;

            *usec = pa_sink_input_set_requested_latency_within_thread(i, *usec);
            return 0;
        }

        case PA_SINK_INPUT_MESSAGE_GET_REQUESTED_LATENCY: {
            pa_usec_t *r = userdata;

            *r = i->thread_info.requested_sink_latency;
            return 0;
        }
    }

    return -PA_ERR_NOTIMPLEMENTED;
}

/* Called from IO context */
bool pa_sink_input_safe_to_remove(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);

    if (PA_SINK_INPUT_IS_LINKED(i->thread_info.state))
        return pa_memblockq_is_empty(i->thread_info.render_memblockq);

    return true;
}

/* Called from IO context */
void pa_sink_input_request_rewind(
        pa_sink_input *i,
        size_t nbytes  /* in our sample spec */,
        bool rewrite,  /* rewrite what we have, or get fresh data? */
        bool flush,    /* flush render memblockq? */
        bool dont_rewind_render) {

    size_t lbq;

    /* If 'rewrite' is true the sink is rewound as far as requested
     * and possible and the exact value of this is passed back the
     * implementor via process_rewind(). If 'flush' is also true all
     * already rendered data is also dropped.
     *
     * If 'rewrite' is false the sink is rewound as far as requested
     * and possible and the already rendered data is dropped so that
     * in the next iteration we read new data from the
     * implementor. This implies 'flush' is true.  If
     * dont_rewind_render is true then the render memblockq is not
     * rewound. */

    /* nbytes = 0 means maximum rewind request */

    pa_sink_input_assert_ref(i);
    pa_sink_input_assert_io_context(i);
    pa_assert(rewrite || flush);
    pa_assert(!dont_rewind_render || !rewrite);

    /* We don't take rewind requests while we are corked */
    if (i->thread_info.state == PA_SINK_INPUT_CORKED)
        return;

    nbytes = PA_MAX(i->thread_info.rewrite_nbytes, nbytes);

#ifdef SINK_INPUT_DEBUG
    pa_log_debug("request rewrite %zu", nbytes);
#endif

    /* Calculate how much we can rewind locally without having to
     * touch the sink */
    if (rewrite)
        lbq = pa_memblockq_get_length(i->thread_info.render_memblockq);
    else
        lbq = 0;

    /* Check if rewinding for the maximum is requested, and if so, fix up */
    if (nbytes <= 0) {

        /* Calculate maximum number of bytes that could be rewound in theory */
        nbytes = i->sink->thread_info.max_rewind + lbq;

        /* Transform from sink domain */
        if (i->thread_info.resampler)
            nbytes = pa_resampler_request(i->thread_info.resampler, nbytes);
    }

    /* Remember how much we actually want to rewrite */
    if (i->thread_info.rewrite_nbytes != (size_t) -1) {
        if (rewrite) {
            /* Make sure to not overwrite over underruns */
            if (nbytes > i->thread_info.playing_for)
                nbytes = (size_t) i->thread_info.playing_for;

            i->thread_info.rewrite_nbytes = nbytes;
        } else
            i->thread_info.rewrite_nbytes = (size_t) -1;
    }

    i->thread_info.rewrite_flush =
        i->thread_info.rewrite_flush || flush;

    i->thread_info.dont_rewind_render =
        i->thread_info.dont_rewind_render ||
        dont_rewind_render;

    /* nbytes is -1 if some earlier rewind request had rewrite == false. */
    if (nbytes != (size_t) -1) {

        /* Transform to sink domain */
        if (i->thread_info.resampler)
            nbytes = pa_resampler_result(i->thread_info.resampler, nbytes);

        if (nbytes > lbq)
            pa_sink_request_rewind(i->sink, nbytes - lbq);
        else
            /* This call will make sure process_rewind() is called later */
            pa_sink_request_rewind(i->sink, 0);
    }
}

/* Called from main context */
pa_memchunk* pa_sink_input_get_silence(pa_sink_input *i, pa_memchunk *ret) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(ret);

    /* FIXME: Shouldn't access resampler object from main context! */

    pa_silence_memchunk_get(
                &i->core->silence_cache,
                i->core->mempool,
                ret,
                &i->sample_spec,
                i->thread_info.resampler ? pa_resampler_max_block_size(i->thread_info.resampler) : 0);

    return ret;
}

/* Called from main context */
void pa_sink_input_send_event(pa_sink_input *i, const char *event, pa_proplist *data) {
    pa_proplist *pl = NULL;
    pa_sink_input_send_event_hook_data hook_data;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(event);

    if (!i->send_event)
        return;

    if (!data)
        data = pl = pa_proplist_new();

    hook_data.sink_input = i;
    hook_data.data = data;
    hook_data.event = event;

    if (pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_SEND_EVENT], &hook_data) < 0)
        goto finish;

    i->send_event(i, event, data);

finish:
    if (pl)
        pa_proplist_free(pl);
}

/* Called from main context */
/* Updates the sink input's resampler with whatever the current sink requires
 * -- useful when the underlying sink's sample spec might have changed */
int pa_sink_input_update_resampler(pa_sink_input *i) {
    pa_resampler *new_resampler;
    char *memblockq_name;

    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();

    if (i->thread_info.resampler &&
        pa_sample_spec_equal(pa_resampler_output_sample_spec(i->thread_info.resampler), &i->sink->sample_spec) &&
        pa_channel_map_equal(pa_resampler_output_channel_map(i->thread_info.resampler), &i->sink->channel_map))

        new_resampler = i->thread_info.resampler;

    else if (!pa_sink_input_is_passthrough(i) &&
        ((i->flags & PA_SINK_INPUT_VARIABLE_RATE) ||
         !pa_sample_spec_equal(&i->sample_spec, &i->sink->sample_spec) ||
         !pa_channel_map_equal(&i->channel_map, &i->sink->channel_map))) {

        new_resampler = pa_resampler_new(i->core->mempool,
                                     &i->sample_spec, &i->channel_map,
                                     &i->sink->sample_spec, &i->sink->channel_map,
                                     i->core->lfe_crossover_freq,
                                     i->requested_resample_method,
                                     ((i->flags & PA_SINK_INPUT_VARIABLE_RATE) ? PA_RESAMPLER_VARIABLE_RATE : 0) |
                                     ((i->flags & PA_SINK_INPUT_NO_REMAP) ? PA_RESAMPLER_NO_REMAP : 0) |
                                     (i->core->disable_remixing || (i->flags & PA_SINK_INPUT_NO_REMIX) ? PA_RESAMPLER_NO_REMIX : 0) |
                                     (i->core->remixing_use_all_sink_channels ? 0 : PA_RESAMPLER_NO_FILL_SINK) |
                                     (i->core->remixing_produce_lfe ? PA_RESAMPLER_PRODUCE_LFE : 0) |
                                     (i->core->remixing_consume_lfe ? PA_RESAMPLER_CONSUME_LFE : 0));

        if (!new_resampler) {
            pa_log_warn("Unsupported resampling operation.");
            return -PA_ERR_NOTSUPPORTED;
        }
    } else
        new_resampler = NULL;

    if (new_resampler == i->thread_info.resampler)
        return 0;

    if (i->thread_info.resampler)
        pa_resampler_free(i->thread_info.resampler);

    i->thread_info.resampler = new_resampler;

    pa_memblockq_free(i->thread_info.render_memblockq);

    memblockq_name = pa_sprintf_malloc("sink input render_memblockq [%u]", i->index);
    i->thread_info.render_memblockq = pa_memblockq_new(
            memblockq_name,
            0,
            MEMBLOCKQ_MAXLENGTH,
            0,
            &i->sink->sample_spec,
            0,
            1,
            0,
            &i->sink->silence);
    pa_xfree(memblockq_name);

    i->actual_resample_method = new_resampler ? pa_resampler_get_method(new_resampler) : PA_RESAMPLER_INVALID;

    pa_log_debug("Updated resampler for sink input %d", i->index);

    return 0;
}

/* Called from the IO thread. */
void pa_sink_input_attach(pa_sink_input *i) {
    pa_assert(i);
    pa_assert(!i->thread_info.attached);

    i->thread_info.attached = true;

    if (i->attach)
        i->attach(i);
}

/* Called from the IO thread. */
void pa_sink_input_detach(pa_sink_input *i) {
    pa_assert(i);

    if (!i->thread_info.attached)
        return;

    i->thread_info.attached = false;

    if (i->detach)
        i->detach(i);
}

/* Called from the main thread. */
void pa_sink_input_set_volume_direct(pa_sink_input *i, const pa_cvolume *volume) {
    pa_cvolume old_volume;
    char old_volume_str[PA_CVOLUME_SNPRINT_VERBOSE_MAX];
    char new_volume_str[PA_CVOLUME_SNPRINT_VERBOSE_MAX];

    pa_assert(i);
    pa_assert(volume);

    old_volume = i->volume;

    if (pa_cvolume_equal(volume, &old_volume))
        return;

    i->volume = *volume;
    pa_log_debug("The volume of sink input %u changed from %s to %s.", i->index,
                 pa_cvolume_snprint_verbose(old_volume_str, sizeof(old_volume_str), &old_volume, &i->channel_map, true),
                 pa_cvolume_snprint_verbose(new_volume_str, sizeof(new_volume_str), volume, &i->channel_map, true));

    if (i->volume_changed)
        i->volume_changed(i);

    pa_subscription_post(i->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_CHANGE, i->index);
    pa_hook_fire(&i->core->hooks[PA_CORE_HOOK_SINK_INPUT_VOLUME_CHANGED], i);
}

/* Called from the main thread. */
void pa_sink_input_set_reference_ratio(pa_sink_input *i, const pa_cvolume *ratio) {
    pa_cvolume old_ratio;
    char old_ratio_str[PA_CVOLUME_SNPRINT_VERBOSE_MAX];
    char new_ratio_str[PA_CVOLUME_SNPRINT_VERBOSE_MAX];

    pa_assert(i);
    pa_assert(ratio);

    old_ratio = i->reference_ratio;

    if (pa_cvolume_equal(ratio, &old_ratio))
        return;

    i->reference_ratio = *ratio;

    if (!PA_SINK_INPUT_IS_LINKED(i->state))
        return;

    pa_log_debug("Sink input %u reference ratio changed from %s to %s.", i->index,
                 pa_cvolume_snprint_verbose(old_ratio_str, sizeof(old_ratio_str), &old_ratio, &i->channel_map, true),
                 pa_cvolume_snprint_verbose(new_ratio_str, sizeof(new_ratio_str), ratio, &i->channel_map, true));
}

/* Called from the main thread. */
void pa_sink_input_set_preferred_sink(pa_sink_input *i, pa_sink *s) {
    pa_assert(i);

    pa_xfree(i->preferred_sink);
    if (s) {
        i->preferred_sink = pa_xstrdup(s->name);
        pa_sink_input_move_to(i, s, false);
    } else {
        i->preferred_sink = NULL;
        pa_sink_input_move_to(i, i->core->default_sink, false);
    }
}
