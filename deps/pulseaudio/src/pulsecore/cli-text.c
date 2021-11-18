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

#include <pulse/volume.h>
#include <pulse/xmalloc.h>
#include <pulse/timeval.h>

#include <pulsecore/module.h>
#include <pulsecore/client.h>
#include <pulsecore/card.h>
#include <pulsecore/sink.h>
#include <pulsecore/source.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/strbuf.h>
#include <pulsecore/core-scache.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>

#include "cli-text.h"

char *pa_module_list_to_string(pa_core *c) {
    pa_strbuf *s;
    pa_module *m;
    uint32_t idx = PA_IDXSET_INVALID;
    pa_assert(c);

    s = pa_strbuf_new();

    pa_strbuf_printf(s, "%u module(s) loaded.\n", pa_idxset_size(c->modules));

    PA_IDXSET_FOREACH(m, c->modules, idx) {
        char *t;

        pa_strbuf_printf(s, "    index: %u\n"
                         "\tname: <%s>\n"
                         "\targument: <%s>\n"
                         "\tused: %i\n"
                         "\tload once: %s\n",
                         m->index,
                         m->name,
                         pa_strempty(m->argument),
                         pa_module_get_n_used(m),
                         pa_yes_no(m->load_once));

        t = pa_proplist_to_string_sep(m->proplist, "\n\t\t");
        pa_strbuf_printf(s, "\tproperties:\n\t\t%s\n", t);
        pa_xfree(t);
    }

    return pa_strbuf_to_string_free(s);
}

char *pa_client_list_to_string(pa_core *c) {
    pa_strbuf *s;
    pa_client *client;
    uint32_t idx = PA_IDXSET_INVALID;
    pa_assert(c);

    s = pa_strbuf_new();

    pa_strbuf_printf(s, "%u client(s) logged in.\n", pa_idxset_size(c->clients));

    PA_IDXSET_FOREACH(client, c->clients, idx) {
        char *t;
        pa_strbuf_printf(
                s,
                "    index: %u\n"
                "\tdriver: <%s>\n",
                client->index,
                client->driver);

        if (client->module)
            pa_strbuf_printf(s, "\towner module: %u\n", client->module->index);

        t = pa_proplist_to_string_sep(client->proplist, "\n\t\t");
        pa_strbuf_printf(s, "\tproperties:\n\t\t%s\n", t);
        pa_xfree(t);
    }

    return pa_strbuf_to_string_free(s);
}

static void append_port_list(pa_strbuf *s, pa_hashmap *ports) {
    pa_device_port *p;
    void *state;

    pa_assert(ports);

    if (pa_hashmap_isempty(ports))
        return;

    pa_strbuf_puts(s, "\tports:\n");
    PA_HASHMAP_FOREACH(p, ports, state) {
        char *t = pa_proplist_to_string_sep(p->proplist, "\n\t\t\t\t");
        pa_strbuf_printf(s, "\t\t%s: %s (priority %u, latency offset %" PRId64 " usec, available: %s)\n",
            p->name, p->description, p->priority, p->latency_offset,
            pa_available_to_string(p->available));
        pa_strbuf_printf(s, "\t\t\tproperties:\n\t\t\t\t%s\n", t);
        pa_xfree(t);
    }
}

char *pa_card_list_to_string(pa_core *c) {
    pa_strbuf *s;
    pa_card *card;
    uint32_t idx = PA_IDXSET_INVALID;
    pa_assert(c);

    s = pa_strbuf_new();

    pa_strbuf_printf(s, "%u card(s) available.\n", pa_idxset_size(c->cards));

    PA_IDXSET_FOREACH(card, c->cards, idx) {
        char *t;
        pa_sink *sink;
        pa_source *source;
        uint32_t sidx;
        pa_card_profile *profile;
        void *state;

        pa_strbuf_printf(
                s,
                "    index: %u\n"
                "\tname: <%s>\n"
                "\tdriver: <%s>\n",
                card->index,
                card->name,
                card->driver);

        if (card->module)
            pa_strbuf_printf(s, "\towner module: %u\n", card->module->index);

        t = pa_proplist_to_string_sep(card->proplist, "\n\t\t");
        pa_strbuf_printf(s, "\tproperties:\n\t\t%s\n", t);
        pa_xfree(t);

        pa_strbuf_puts(s, "\tprofiles:\n");
        PA_HASHMAP_FOREACH(profile, card->profiles, state)
            pa_strbuf_printf(s, "\t\t%s: %s (priority %u, available: %s)\n", profile->name, profile->description,
                             profile->priority, pa_available_to_string(profile->available));

        pa_strbuf_printf(
                s,
                "\tactive profile: <%s>\n",
                card->active_profile->name);

        if (!pa_idxset_isempty(card->sinks)) {
            pa_strbuf_puts(s, "\tsinks:\n");
            PA_IDXSET_FOREACH(sink, card->sinks, sidx)
                pa_strbuf_printf(s, "\t\t%s/#%u: %s\n", sink->name, sink->index, pa_strna(pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_DESCRIPTION)));
        }

        if (!pa_idxset_isempty(card->sources)) {
            pa_strbuf_puts(s, "\tsources:\n");
            PA_IDXSET_FOREACH(source, card->sources, sidx)
                pa_strbuf_printf(s, "\t\t%s/#%u: %s\n", source->name, source->index, pa_strna(pa_proplist_gets(source->proplist, PA_PROP_DEVICE_DESCRIPTION)));
        }

        append_port_list(s, card->ports);
    }

    return pa_strbuf_to_string_free(s);
}

char *pa_sink_list_to_string(pa_core *c) {
    pa_strbuf *s;
    pa_sink *sink;
    uint32_t idx = PA_IDXSET_INVALID;
    pa_assert(c);

    s = pa_strbuf_new();

    pa_strbuf_printf(s, "%u sink(s) available.\n", pa_idxset_size(c->sinks));

    PA_IDXSET_FOREACH(sink, c->sinks, idx) {
        char ss[PA_SAMPLE_SPEC_SNPRINT_MAX],
            cv[PA_CVOLUME_SNPRINT_VERBOSE_MAX],
            v[PA_VOLUME_SNPRINT_VERBOSE_MAX],
            cm[PA_CHANNEL_MAP_SNPRINT_MAX], *t;
        const char *cmn;
        char suspend_cause_buf[PA_SUSPEND_CAUSE_TO_STRING_BUF_SIZE];

        cmn = pa_channel_map_to_pretty_name(&sink->channel_map);

        pa_strbuf_printf(
            s,
            "  %c index: %u\n"
            "\tname: <%s>\n"
            "\tdriver: <%s>\n"
            "\tflags: %s%s%s%s%s%s%s%s\n"
            "\tstate: %s\n"
            "\tsuspend cause: %s\n"
            "\tpriority: %u\n"
            "\tvolume: %s\n"
            "\t        balance %0.2f\n"
            "\tbase volume: %s\n"
            "\tvolume steps: %u\n"
            "\tmuted: %s\n"
            "\tcurrent latency: %0.2f ms\n"
            "\tmax request: %lu KiB\n"
            "\tmax rewind: %lu KiB\n"
            "\tmonitor source: %u\n"
            "\tsample spec: %s\n"
            "\tchannel map: %s%s%s\n"
            "\tused by: %u\n"
            "\tlinked by: %u\n",
            sink == c->default_sink ? '*' : ' ',
            sink->index,
            sink->name,
            sink->driver,
            sink->flags & PA_SINK_HARDWARE ? "HARDWARE " : "",
            sink->flags & PA_SINK_NETWORK ? "NETWORK " : "",
            sink->flags & PA_SINK_HW_MUTE_CTRL ? "HW_MUTE_CTRL " : "",
            sink->flags & PA_SINK_HW_VOLUME_CTRL ? "HW_VOLUME_CTRL " : "",
            sink->flags & PA_SINK_DECIBEL_VOLUME ? "DECIBEL_VOLUME " : "",
            sink->flags & PA_SINK_LATENCY ? "LATENCY " : "",
            sink->flags & PA_SINK_FLAT_VOLUME ? "FLAT_VOLUME " : "",
            sink->flags & PA_SINK_DYNAMIC_LATENCY ? "DYNAMIC_LATENCY" : "",
            pa_sink_state_to_string(sink->state),
            pa_suspend_cause_to_string(sink->suspend_cause, suspend_cause_buf),
            sink->priority,
            pa_cvolume_snprint_verbose(cv,
                                       sizeof(cv),
                                       pa_sink_get_volume(sink, false),
                                       &sink->channel_map,
                                       sink->flags & PA_SINK_DECIBEL_VOLUME),
            pa_cvolume_get_balance(pa_sink_get_volume(sink, false), &sink->channel_map),
            pa_volume_snprint_verbose(v, sizeof(v), sink->base_volume, sink->flags & PA_SINK_DECIBEL_VOLUME),
            sink->n_volume_steps,
            pa_yes_no(pa_sink_get_mute(sink, false)),
            (double) pa_sink_get_latency(sink) / (double) PA_USEC_PER_MSEC,
            (unsigned long) pa_sink_get_max_request(sink) / 1024,
            (unsigned long) pa_sink_get_max_rewind(sink) / 1024,
            sink->monitor_source ? sink->monitor_source->index : PA_INVALID_INDEX,
            pa_sample_spec_snprint(ss, sizeof(ss), &sink->sample_spec),
            pa_channel_map_snprint(cm, sizeof(cm), &sink->channel_map),
            cmn ? "\n\t             " : "",
            cmn ? cmn : "",
            pa_sink_used_by(sink),
            pa_sink_linked_by(sink));

        if (sink->flags & PA_SINK_DYNAMIC_LATENCY) {
            pa_usec_t min_latency, max_latency;
            pa_sink_get_latency_range(sink, &min_latency, &max_latency);

            pa_strbuf_printf(
                    s,
                    "\tconfigured latency: %0.2f ms; range is %0.2f .. %0.2f ms\n",
                    (double) pa_sink_get_requested_latency(sink) / (double) PA_USEC_PER_MSEC,
                    (double) min_latency / PA_USEC_PER_MSEC,
                    (double) max_latency / PA_USEC_PER_MSEC);
        } else
            pa_strbuf_printf(
                    s,
                    "\tfixed latency: %0.2f ms\n",
                    (double) pa_sink_get_fixed_latency(sink) / PA_USEC_PER_MSEC);

        if (sink->card)
            pa_strbuf_printf(s, "\tcard: %u <%s>\n", sink->card->index, sink->card->name);
        if (sink->module)
            pa_strbuf_printf(s, "\tmodule: %u\n", sink->module->index);

        t = pa_proplist_to_string_sep(sink->proplist, "\n\t\t");
        pa_strbuf_printf(s, "\tproperties:\n\t\t%s\n", t);
        pa_xfree(t);

        append_port_list(s, sink->ports);

        if (sink->active_port)
            pa_strbuf_printf(
                    s,
                    "\tactive port: <%s>\n",
                    sink->active_port->name);
    }

    return pa_strbuf_to_string_free(s);
}

char *pa_source_list_to_string(pa_core *c) {
    pa_strbuf *s;
    pa_source *source;
    uint32_t idx = PA_IDXSET_INVALID;
    pa_assert(c);

    s = pa_strbuf_new();

    pa_strbuf_printf(s, "%u source(s) available.\n", pa_idxset_size(c->sources));

    PA_IDXSET_FOREACH(source, c->sources, idx) {
        char ss[PA_SAMPLE_SPEC_SNPRINT_MAX],
            cv[PA_CVOLUME_SNPRINT_VERBOSE_MAX],
            v[PA_VOLUME_SNPRINT_VERBOSE_MAX],
            cm[PA_CHANNEL_MAP_SNPRINT_MAX], *t;
        const char *cmn;
        char suspend_cause_buf[PA_SUSPEND_CAUSE_TO_STRING_BUF_SIZE];

        cmn = pa_channel_map_to_pretty_name(&source->channel_map);

        pa_strbuf_printf(
            s,
            "  %c index: %u\n"
            "\tname: <%s>\n"
            "\tdriver: <%s>\n"
            "\tflags: %s%s%s%s%s%s%s\n"
            "\tstate: %s\n"
            "\tsuspend cause: %s\n"
            "\tpriority: %u\n"
            "\tvolume: %s\n"
            "\t        balance %0.2f\n"
            "\tbase volume: %s\n"
            "\tvolume steps: %u\n"
            "\tmuted: %s\n"
            "\tcurrent latency: %0.2f ms\n"
            "\tmax rewind: %lu KiB\n"
            "\tsample spec: %s\n"
            "\tchannel map: %s%s%s\n"
            "\tused by: %u\n"
            "\tlinked by: %u\n",
            source == c->default_source ? '*' : ' ',
            source->index,
            source->name,
            source->driver,
            source->flags & PA_SOURCE_HARDWARE ? "HARDWARE " : "",
            source->flags & PA_SOURCE_NETWORK ? "NETWORK " : "",
            source->flags & PA_SOURCE_HW_MUTE_CTRL ? "HW_MUTE_CTRL " : "",
            source->flags & PA_SOURCE_HW_VOLUME_CTRL ? "HW_VOLUME_CTRL " : "",
            source->flags & PA_SOURCE_DECIBEL_VOLUME ? "DECIBEL_VOLUME " : "",
            source->flags & PA_SOURCE_LATENCY ? "LATENCY " : "",
            source->flags & PA_SOURCE_DYNAMIC_LATENCY ? "DYNAMIC_LATENCY" : "",
            pa_source_state_to_string(source->state),
            pa_suspend_cause_to_string(source->suspend_cause, suspend_cause_buf),
            source->priority,
            pa_cvolume_snprint_verbose(cv,
                                       sizeof(cv),
                                       pa_source_get_volume(source, false),
                                       &source->channel_map,
                                       source->flags & PA_SOURCE_DECIBEL_VOLUME),
            pa_cvolume_get_balance(pa_source_get_volume(source, false), &source->channel_map),
            pa_volume_snprint_verbose(v, sizeof(v), source->base_volume, source->flags & PA_SOURCE_DECIBEL_VOLUME),
            source->n_volume_steps,
            pa_yes_no(pa_source_get_mute(source, false)),
            (double) pa_source_get_latency(source) / PA_USEC_PER_MSEC,
            (unsigned long) pa_source_get_max_rewind(source) / 1024,
            pa_sample_spec_snprint(ss, sizeof(ss), &source->sample_spec),
            pa_channel_map_snprint(cm, sizeof(cm), &source->channel_map),
            cmn ? "\n\t             " : "",
            cmn ? cmn : "",
            pa_source_used_by(source),
            pa_source_linked_by(source));

        if (source->flags & PA_SOURCE_DYNAMIC_LATENCY) {
            pa_usec_t min_latency, max_latency;
            pa_source_get_latency_range(source, &min_latency, &max_latency);

            pa_strbuf_printf(
                    s,
                    "\tconfigured latency: %0.2f ms; range is %0.2f .. %0.2f ms\n",
                    (double) pa_source_get_requested_latency(source) / PA_USEC_PER_MSEC,
                    (double) min_latency / PA_USEC_PER_MSEC,
                    (double) max_latency / PA_USEC_PER_MSEC);
        } else
            pa_strbuf_printf(
                    s,
                    "\tfixed latency: %0.2f ms\n",
                    (double) pa_source_get_fixed_latency(source) / PA_USEC_PER_MSEC);

        if (source->monitor_of)
            pa_strbuf_printf(s, "\tmonitor_of: %u\n", source->monitor_of->index);
        if (source->card)
            pa_strbuf_printf(s, "\tcard: %u <%s>\n", source->card->index, source->card->name);
        if (source->module)
            pa_strbuf_printf(s, "\tmodule: %u\n", source->module->index);

        t = pa_proplist_to_string_sep(source->proplist, "\n\t\t");
        pa_strbuf_printf(s, "\tproperties:\n\t\t%s\n", t);
        pa_xfree(t);

        append_port_list(s, source->ports);

        if (source->active_port)
            pa_strbuf_printf(
                    s,
                    "\tactive port: <%s>\n",
                    source->active_port->name);
    }

    return pa_strbuf_to_string_free(s);
}

char *pa_source_output_list_to_string(pa_core *c) {
    pa_strbuf *s;
    pa_source_output *o;
    uint32_t idx = PA_IDXSET_INVALID;
    static const char* const state_table[] = {
        [PA_SOURCE_OUTPUT_INIT] = "INIT",
        [PA_SOURCE_OUTPUT_RUNNING] = "RUNNING",
        [PA_SOURCE_OUTPUT_CORKED] = "CORKED",
        [PA_SOURCE_OUTPUT_UNLINKED] = "UNLINKED"
    };
    pa_assert(c);

    s = pa_strbuf_new();

    pa_strbuf_printf(s, "%u source output(s) available.\n", pa_idxset_size(c->source_outputs));

    PA_IDXSET_FOREACH(o, c->source_outputs, idx) {
        char ss[PA_SAMPLE_SPEC_SNPRINT_MAX], cv[PA_CVOLUME_SNPRINT_VERBOSE_MAX], cm[PA_CHANNEL_MAP_SNPRINT_MAX], *t, clt[28];
        pa_usec_t cl;
        const char *cmn;
        pa_cvolume v;
        char *volume_str = NULL;

        cmn = pa_channel_map_to_pretty_name(&o->channel_map);

        if ((cl = pa_source_output_get_requested_latency(o)) == (pa_usec_t) -1)
            pa_snprintf(clt, sizeof(clt), "n/a");
        else
            pa_snprintf(clt, sizeof(clt), "%0.2f ms", (double) cl / PA_USEC_PER_MSEC);

        pa_assert(o->source);

        if (pa_source_output_is_volume_readable(o)) {
            pa_source_output_get_volume(o, &v, true);
            volume_str = pa_sprintf_malloc("%s\n\t        balance %0.2f",
                                           pa_cvolume_snprint_verbose(cv, sizeof(cv), &v, &o->channel_map, true),
                                           pa_cvolume_get_balance(&v, &o->channel_map));
        } else
            volume_str = pa_xstrdup("n/a");

        pa_strbuf_printf(
            s,
            "    index: %u\n"
            "\tdriver: <%s>\n"
            "\tflags: %s%s%s%s%s%s%s%s%s%s%s%s\n"
            "\tstate: %s\n"
            "\tsource: %u <%s>\n"
            "\tvolume: %s\n"
            "\tmuted: %s\n"
            "\tcurrent latency: %0.2f ms\n"
            "\trequested latency: %s\n"
            "\tsample spec: %s\n"
            "\tchannel map: %s%s%s\n"
            "\tresample method: %s\n",
            o->index,
            o->driver,
            o->flags & PA_SOURCE_OUTPUT_VARIABLE_RATE ? "VARIABLE_RATE " : "",
            o->flags & PA_SOURCE_OUTPUT_DONT_MOVE ? "DONT_MOVE " : "",
            o->flags & PA_SOURCE_OUTPUT_START_CORKED ? "START_CORKED " : "",
            o->flags & PA_SOURCE_OUTPUT_NO_REMAP ? "NO_REMAP " : "",
            o->flags & PA_SOURCE_OUTPUT_NO_REMIX ? "NO_REMIX " : "",
            o->flags & PA_SOURCE_OUTPUT_FIX_FORMAT ? "FIX_FORMAT " : "",
            o->flags & PA_SOURCE_OUTPUT_FIX_RATE ? "FIX_RATE " : "",
            o->flags & PA_SOURCE_OUTPUT_FIX_CHANNELS ? "FIX_CHANNELS " : "",
            o->flags & PA_SOURCE_OUTPUT_DONT_INHIBIT_AUTO_SUSPEND ? "DONT_INHIBIT_AUTO_SUSPEND " : "",
            o->flags & PA_SOURCE_OUTPUT_NO_CREATE_ON_SUSPEND ? "NO_CREATE_ON_SUSPEND " : "",
            o->flags & PA_SOURCE_OUTPUT_KILL_ON_SUSPEND ? "KILL_ON_SUSPEND " : "",
            o->flags & PA_SOURCE_OUTPUT_PASSTHROUGH ? "PASSTHROUGH " : "",
            state_table[o->state],
            o->source->index, o->source->name,
            volume_str,
            pa_yes_no(o->muted),
            (double) pa_source_output_get_latency(o, NULL) / PA_USEC_PER_MSEC,
            clt,
            pa_sample_spec_snprint(ss, sizeof(ss), &o->sample_spec),
            pa_channel_map_snprint(cm, sizeof(cm), &o->channel_map),
            cmn ? "\n\t             " : "",
            cmn ? cmn : "",
            pa_resample_method_to_string(pa_source_output_get_resample_method(o)));

        pa_xfree(volume_str);

        if (o->module)
            pa_strbuf_printf(s, "\towner module: %u\n", o->module->index);
        if (o->client)
            pa_strbuf_printf(s, "\tclient: %u <%s>\n", o->client->index, pa_strnull(pa_proplist_gets(o->client->proplist, PA_PROP_APPLICATION_NAME)));
        if (o->direct_on_input)
            pa_strbuf_printf(s, "\tdirect on input: %u\n", o->direct_on_input->index);

        t = pa_proplist_to_string_sep(o->proplist, "\n\t\t");
        pa_strbuf_printf(s, "\tproperties:\n\t\t%s\n", t);
        pa_xfree(t);
    }

    return pa_strbuf_to_string_free(s);
}

char *pa_sink_input_list_to_string(pa_core *c) {
    pa_strbuf *s;
    pa_sink_input *i;
    uint32_t idx = PA_IDXSET_INVALID;
    static const char* const state_table[] = {
        [PA_SINK_INPUT_INIT] = "INIT",
        [PA_SINK_INPUT_RUNNING] = "RUNNING",
        [PA_SINK_INPUT_CORKED] = "CORKED",
        [PA_SINK_INPUT_UNLINKED] = "UNLINKED"
    };

    pa_assert(c);
    s = pa_strbuf_new();

    pa_strbuf_printf(s, "%u sink input(s) available.\n", pa_idxset_size(c->sink_inputs));

    PA_IDXSET_FOREACH(i, c->sink_inputs, idx) {
        char ss[PA_SAMPLE_SPEC_SNPRINT_MAX], cv[PA_CVOLUME_SNPRINT_MAX], cm[PA_CHANNEL_MAP_SNPRINT_MAX], *t, clt[28];
        pa_usec_t cl;
        const char *cmn;
        pa_cvolume v;
        char *volume_str = NULL;

        cmn = pa_channel_map_to_pretty_name(&i->channel_map);

        if ((cl = pa_sink_input_get_requested_latency(i)) == (pa_usec_t) -1)
            pa_snprintf(clt, sizeof(clt), "n/a");
        else
            pa_snprintf(clt, sizeof(clt), "%0.2f ms", (double) cl / PA_USEC_PER_MSEC);

        pa_assert(i->sink);

        if (pa_sink_input_is_volume_readable(i)) {
            pa_sink_input_get_volume(i, &v, true);
            volume_str = pa_sprintf_malloc("%s\n\t        balance %0.2f",
                                           pa_cvolume_snprint_verbose(cv, sizeof(cv), &v, &i->channel_map, true),
                                           pa_cvolume_get_balance(&v, &i->channel_map));
        } else
            volume_str = pa_xstrdup("n/a");

        pa_strbuf_printf(
            s,
            "    index: %u\n"
            "\tdriver: <%s>\n"
            "\tflags: %s%s%s%s%s%s%s%s%s%s%s%s\n"
            "\tstate: %s\n"
            "\tsink: %u <%s>\n"
            "\tvolume: %s\n"
            "\tmuted: %s\n"
            "\tcurrent latency: %0.2f ms\n"
            "\trequested latency: %s\n"
            "\tsample spec: %s\n"
            "\tchannel map: %s%s%s\n"
            "\tresample method: %s\n",
            i->index,
            i->driver,
            i->flags & PA_SINK_INPUT_VARIABLE_RATE ? "VARIABLE_RATE " : "",
            i->flags & PA_SINK_INPUT_DONT_MOVE ? "DONT_MOVE " : "",
            i->flags & PA_SINK_INPUT_START_CORKED ? "START_CORKED " : "",
            i->flags & PA_SINK_INPUT_NO_REMAP ? "NO_REMAP " : "",
            i->flags & PA_SINK_INPUT_NO_REMIX ? "NO_REMIX " : "",
            i->flags & PA_SINK_INPUT_FIX_FORMAT ? "FIX_FORMAT " : "",
            i->flags & PA_SINK_INPUT_FIX_RATE ? "FIX_RATE " : "",
            i->flags & PA_SINK_INPUT_FIX_CHANNELS ? "FIX_CHANNELS " : "",
            i->flags & PA_SINK_INPUT_DONT_INHIBIT_AUTO_SUSPEND ? "DONT_INHIBIT_AUTO_SUSPEND " : "",
            i->flags & PA_SINK_INPUT_NO_CREATE_ON_SUSPEND ? "NO_CREATE_SUSPEND " : "",
            i->flags & PA_SINK_INPUT_KILL_ON_SUSPEND ? "KILL_ON_SUSPEND " : "",
            i->flags & PA_SINK_INPUT_PASSTHROUGH ? "PASSTHROUGH " : "",
            state_table[i->state],
            i->sink->index, i->sink->name,
            volume_str,
            pa_yes_no(i->muted),
            (double) pa_sink_input_get_latency(i, NULL) / PA_USEC_PER_MSEC,
            clt,
            pa_sample_spec_snprint(ss, sizeof(ss), &i->sample_spec),
            pa_channel_map_snprint(cm, sizeof(cm), &i->channel_map),
            cmn ? "\n\t             " : "",
            cmn ? cmn : "",
            pa_resample_method_to_string(pa_sink_input_get_resample_method(i)));

        pa_xfree(volume_str);

        if (i->module)
            pa_strbuf_printf(s, "\tmodule: %u\n", i->module->index);
        if (i->client)
            pa_strbuf_printf(s, "\tclient: %u <%s>\n", i->client->index, pa_strnull(pa_proplist_gets(i->client->proplist, PA_PROP_APPLICATION_NAME)));

        t = pa_proplist_to_string_sep(i->proplist, "\n\t\t");
        pa_strbuf_printf(s, "\tproperties:\n\t\t%s\n", t);
        pa_xfree(t);
    }

    return pa_strbuf_to_string_free(s);
}

char *pa_scache_list_to_string(pa_core *c) {
    pa_strbuf *s;
    pa_assert(c);

    s = pa_strbuf_new();

    pa_strbuf_printf(s, "%u cache entrie(s) available.\n", c->scache ? pa_idxset_size(c->scache) : 0);

    if (c->scache) {
        pa_scache_entry *e;
        uint32_t idx = PA_IDXSET_INVALID;

        PA_IDXSET_FOREACH(e, c->scache, idx) {
            double l = 0;
            char ss[PA_SAMPLE_SPEC_SNPRINT_MAX] = "n/a", cv[PA_CVOLUME_SNPRINT_MAX], cm[PA_CHANNEL_MAP_SNPRINT_MAX] = "n/a", *t;
            const char *cmn;

            cmn = pa_channel_map_to_pretty_name(&e->channel_map);

            if (e->memchunk.memblock) {
                pa_sample_spec_snprint(ss, sizeof(ss), &e->sample_spec);
                pa_channel_map_snprint(cm, sizeof(cm), &e->channel_map);
                l = (double) e->memchunk.length / (double) pa_bytes_per_second(&e->sample_spec);
            }

            pa_strbuf_printf(
                s,
                "    name: <%s>\n"
                "\tindex: %u\n"
                "\tsample spec: %s\n"
                "\tchannel map: %s%s%s\n"
                "\tlength: %lu\n"
                "\tduration: %0.1f s\n"
                "\tvolume: %s\n"
                "\t        balance %0.2f\n"
                "\tlazy: %s\n"
                "\tfilename: <%s>\n",
                e->name,
                e->index,
                ss,
                cm,
                cmn ? "\n\t             " : "",
                cmn ? cmn : "",
                (long unsigned)(e->memchunk.memblock ? e->memchunk.length : 0),
                l,
                e->volume_is_set ? pa_cvolume_snprint_verbose(cv, sizeof(cv), &e->volume, &e->channel_map, true) : "n/a",
                (e->memchunk.memblock && e->volume_is_set) ? pa_cvolume_get_balance(&e->volume, &e->channel_map) : 0.0f,
                pa_yes_no(e->lazy),
                e->filename ? e->filename : "n/a");

            t = pa_proplist_to_string_sep(e->proplist, "\n\t\t");
            pa_strbuf_printf(s, "\tproperties:\n\t\t%s\n", t);
            pa_xfree(t);
        }
    }

    return pa_strbuf_to_string_free(s);
}

char *pa_full_status_string(pa_core *c) {
    pa_strbuf *s;
    int i;

    s = pa_strbuf_new();

    for (i = 0; i < 8; i++) {
        char *t = NULL;

        switch (i) {
            case 0:
                t = pa_sink_list_to_string(c);
                break;
            case 1:
                t = pa_source_list_to_string(c);
                break;
            case 2:
                t = pa_sink_input_list_to_string(c);
                break;
            case 3:
                t = pa_source_output_list_to_string(c);
                break;
            case 4:
                t = pa_client_list_to_string(c);
                break;
            case 5:
                t = pa_card_list_to_string(c);
                break;
            case 6:
                t = pa_module_list_to_string(c);
                break;
            case 7:
                t = pa_scache_list_to_string(c);
                break;
        }

        pa_strbuf_puts(s, t);
        pa_xfree(t);
    }

    return pa_strbuf_to_string_free(s);
}
