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

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/module.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-scache.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/random.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "core.h"

PA_DEFINE_PUBLIC_CLASS(pa_core, pa_msgobject);

static int core_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk) {
    pa_core *c = PA_CORE(o);

    pa_core_assert_ref(c);

    switch (code) {

        case PA_CORE_MESSAGE_UNLOAD_MODULE:
            pa_module_unload(userdata, true);
            return 0;

        default:
            return -1;
    }
}

static void core_free(pa_object *o);

pa_core* pa_core_new(pa_mainloop_api *m, bool shared, bool enable_memfd, size_t shm_size) {
    pa_core* c;
    pa_mempool *pool;
    pa_mem_type_t type;
    int j;

    pa_assert(m);

    if (shared) {
        type = (enable_memfd) ? PA_MEM_TYPE_SHARED_MEMFD : PA_MEM_TYPE_SHARED_POSIX;
        if (!(pool = pa_mempool_new(type, shm_size, false))) {
            pa_log_warn("Failed to allocate %s memory pool. Falling back to a normal memory pool.",
                        pa_mem_type_to_string(type));
            shared = false;
        }
    }

    if (!shared) {
        if (!(pool = pa_mempool_new(PA_MEM_TYPE_PRIVATE, shm_size, false))) {
            pa_log("pa_mempool_new() failed.");
            return NULL;
        }
    }

    c = pa_msgobject_new(pa_core);
    c->parent.parent.free = core_free;
    c->parent.process_msg = core_process_msg;

    c->state = PA_CORE_STARTUP;
    c->mainloop = m;

    c->clients = pa_idxset_new(NULL, NULL);
    c->cards = pa_idxset_new(NULL, NULL);
    c->sinks = pa_idxset_new(NULL, NULL);
    c->sources = pa_idxset_new(NULL, NULL);
    c->sink_inputs = pa_idxset_new(NULL, NULL);
    c->source_outputs = pa_idxset_new(NULL, NULL);
    c->modules = pa_idxset_new(NULL, NULL);
    c->scache = pa_idxset_new(NULL, NULL);

    c->namereg = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    c->shared = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    c->message_handlers = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    c->default_source = NULL;
    c->default_sink = NULL;

    c->default_sample_spec.format = PA_SAMPLE_S16NE;
    c->default_sample_spec.rate = 44100;
    c->default_sample_spec.channels = 2;
    pa_channel_map_init_extend(&c->default_channel_map, c->default_sample_spec.channels, PA_CHANNEL_MAP_DEFAULT);
    c->default_n_fragments = 4;
    c->default_fragment_size_msec = 25;

    c->deferred_volume_safety_margin_usec = 8000;
    c->deferred_volume_extra_delay_usec = 0;

    c->module_defer_unload_event = NULL;
    c->modules_pending_unload = pa_hashmap_new(NULL, NULL);

    c->subscription_defer_event = NULL;
    PA_LLIST_HEAD_INIT(pa_subscription, c->subscriptions);
    PA_LLIST_HEAD_INIT(pa_subscription_event, c->subscription_event_queue);
    c->subscription_event_last = NULL;

    c->mempool = pool;
    c->shm_size = shm_size;
    pa_silence_cache_init(&c->silence_cache);

    c->exit_event = NULL;
    c->scache_auto_unload_event = NULL;

    c->exit_idle_time = -1;
    c->scache_idle_time = 20;

    c->flat_volumes = true;
    c->disallow_module_loading = false;
    c->disallow_exit = false;
    c->running_as_daemon = false;
    c->realtime_scheduling = false;
    c->realtime_priority = 5;
    c->disable_remixing = false;
    c->remixing_use_all_sink_channels = true;
    c->remixing_produce_lfe = false;
    c->remixing_consume_lfe = false;
    c->lfe_crossover_freq = 0;
    c->deferred_volume = true;
    c->resample_method = PA_RESAMPLER_SPEEX_FLOAT_BASE + 1;

    for (j = 0; j < PA_CORE_HOOK_MAX; j++)
        pa_hook_init(&c->hooks[j], c);

    pa_random(&c->cookie, sizeof(c->cookie));

#ifdef SIGPIPE
    pa_check_signal_is_blocked(SIGPIPE);
#endif

    return c;
}

static void core_free(pa_object *o) {
    pa_core *c = PA_CORE(o);
    int j;
    pa_assert(c);

    c->state = PA_CORE_SHUTDOWN;

    /* Note: All modules and samples in the cache should be unloaded before
     * we get here */

    pa_assert(pa_idxset_isempty(c->scache));
    pa_idxset_free(c->scache, NULL);

    pa_assert(pa_idxset_isempty(c->modules));
    pa_idxset_free(c->modules, NULL);

    pa_assert(pa_idxset_isempty(c->clients));
    pa_idxset_free(c->clients, NULL);

    pa_assert(pa_idxset_isempty(c->cards));
    pa_idxset_free(c->cards, NULL);

    pa_assert(pa_idxset_isempty(c->sinks));
    pa_idxset_free(c->sinks, NULL);

    pa_assert(pa_idxset_isempty(c->sources));
    pa_idxset_free(c->sources, NULL);

    pa_assert(pa_idxset_isempty(c->source_outputs));
    pa_idxset_free(c->source_outputs, NULL);

    pa_assert(pa_idxset_isempty(c->sink_inputs));
    pa_idxset_free(c->sink_inputs, NULL);

    pa_assert(pa_hashmap_isempty(c->namereg));
    pa_hashmap_free(c->namereg);

    pa_assert(pa_hashmap_isempty(c->shared));
    pa_hashmap_free(c->shared);

    pa_assert(pa_hashmap_isempty(c->message_handlers));
    pa_hashmap_free(c->message_handlers);

    pa_assert(pa_hashmap_isempty(c->modules_pending_unload));
    pa_hashmap_free(c->modules_pending_unload);

    pa_subscription_free_all(c);

    if (c->exit_event)
        c->mainloop->time_free(c->exit_event);

    pa_assert(!c->default_source);
    pa_assert(!c->default_sink);
    pa_xfree(c->configured_default_source);
    pa_xfree(c->configured_default_sink);

    pa_silence_cache_done(&c->silence_cache);
    pa_mempool_unref(c->mempool);

    for (j = 0; j < PA_CORE_HOOK_MAX; j++)
        pa_hook_done(&c->hooks[j]);

    pa_xfree(c);
}

void pa_core_set_configured_default_sink(pa_core *core, const char *sink) {
    char *old_sink;

    pa_assert(core);

    old_sink = pa_xstrdup(core->configured_default_sink);

    if (pa_safe_streq(sink, old_sink))
        goto finish;

    pa_xfree(core->configured_default_sink);
    core->configured_default_sink = pa_xstrdup(sink);
    pa_log_info("configured_default_sink: %s -> %s",
                old_sink ? old_sink : "(unset)", sink ? sink : "(unset)");
    pa_subscription_post(core, PA_SUBSCRIPTION_EVENT_SERVER | PA_SUBSCRIPTION_EVENT_CHANGE, PA_INVALID_INDEX);

    pa_core_update_default_sink(core);

finish:
    pa_xfree(old_sink);
}

void pa_core_set_configured_default_source(pa_core *core, const char *source) {
    char *old_source;

    pa_assert(core);

    old_source = pa_xstrdup(core->configured_default_source);

    if (pa_safe_streq(source, old_source))
        goto finish;

    pa_xfree(core->configured_default_source);
    core->configured_default_source = pa_xstrdup(source);
    pa_log_info("configured_default_source: %s -> %s",
                old_source ? old_source : "(unset)", source ? source : "(unset)");
    pa_subscription_post(core, PA_SUBSCRIPTION_EVENT_SERVER | PA_SUBSCRIPTION_EVENT_CHANGE, PA_INVALID_INDEX);

    pa_core_update_default_source(core);

finish:
    pa_xfree(old_source);
}

/* a  < b  ->  return -1
 * a == b  ->  return  0
 * a  > b  ->  return  1 */
static int compare_sinks(pa_sink *a, pa_sink *b) {
    pa_core *core;

    core = a->core;

    /* Available sinks always beat unavailable sinks. */
    if (a->active_port && a->active_port->available == PA_AVAILABLE_NO
            && (!b->active_port || b->active_port->available != PA_AVAILABLE_NO))
        return -1;
    if (b->active_port && b->active_port->available == PA_AVAILABLE_NO
            && (!a->active_port || a->active_port->available != PA_AVAILABLE_NO))
        return 1;

    /* The configured default sink is preferred over any other sink. */
    if (pa_safe_streq(b->name, core->configured_default_sink))
        return -1;
    if (pa_safe_streq(a->name, core->configured_default_sink))
        return 1;

    if (a->priority < b->priority)
        return -1;
    if (a->priority > b->priority)
        return 1;

    /* It's hard to find any difference between these sinks, but maybe one of
     * them is already the default sink? If so, it's best to keep it as the
     * default to avoid changing the routing for no good reason. */
    if (b == core->default_sink)
        return -1;
    if (a == core->default_sink)
        return 1;

    return 0;
}

void pa_core_update_default_sink(pa_core *core) {
    pa_sink *best = NULL;
    pa_sink *sink;
    uint32_t idx;
    pa_sink *old_default_sink;

    pa_assert(core);

    PA_IDXSET_FOREACH(sink, core->sinks, idx) {
        if (!PA_SINK_IS_LINKED(sink->state))
            continue;

        if (!best) {
            best = sink;
            continue;
        }

        if (compare_sinks(sink, best) > 0)
            best = sink;
    }

    old_default_sink = core->default_sink;

    if (best == old_default_sink)
        return;

    core->default_sink = best;
    pa_log_info("default_sink: %s -> %s",
                old_default_sink ? old_default_sink->name : "(unset)", best ? best->name : "(unset)");

    /* If the default sink changed, it may be that the default source has to be
     * changed too, because monitor sources are prioritized partly based on the
     * priorities of the monitored sinks. */
    pa_core_update_default_source(core);

    pa_subscription_post(core, PA_SUBSCRIPTION_EVENT_SERVER | PA_SUBSCRIPTION_EVENT_CHANGE, PA_INVALID_INDEX);
    pa_hook_fire(&core->hooks[PA_CORE_HOOK_DEFAULT_SINK_CHANGED], core->default_sink);

    /* try to move the streams from old_default_sink to the new default_sink conditionally */
    if (old_default_sink)
        pa_sink_move_streams_to_default_sink(core, old_default_sink, true);
}

/* a  < b  ->  return -1
 * a == b  ->  return  0
 * a  > b  ->  return  1 */
static int compare_sources(pa_source *a, pa_source *b) {
    pa_core *core;

    core = a->core;

    /* Available sources always beat unavailable sources. */
    if (a->active_port && a->active_port->available == PA_AVAILABLE_NO
            && (!b->active_port || b->active_port->available != PA_AVAILABLE_NO))
        return -1;
    if (b->active_port && b->active_port->available == PA_AVAILABLE_NO
            && (!a->active_port || a->active_port->available != PA_AVAILABLE_NO))
        return 1;

    /* The configured default source is preferred over any other source. */
    if (pa_safe_streq(b->name, core->configured_default_source))
        return -1;
    if (pa_safe_streq(a->name, core->configured_default_source))
        return 1;

    /* Monitor sources lose to non-monitor sources. */
    if (a->monitor_of && !b->monitor_of)
        return -1;
    if (!a->monitor_of && b->monitor_of)
        return 1;

    if (a->priority < b->priority)
        return -1;
    if (a->priority > b->priority)
        return 1;

    /* If the sources are monitors, we can compare the monitored sinks. */
    if (a->monitor_of)
        return compare_sinks(a->monitor_of, b->monitor_of);

    /* It's hard to find any difference between these sources, but maybe one of
     * them is already the default source? If so, it's best to keep it as the
     * default to avoid changing the routing for no good reason. */
    if (b == core->default_source)
        return -1;
    if (a == core->default_source)
        return 1;

    return 0;
}

void pa_core_update_default_source(pa_core *core) {
    pa_source *best = NULL;
    pa_source *source;
    uint32_t idx;
    pa_source *old_default_source;

    pa_assert(core);

    PA_IDXSET_FOREACH(source, core->sources, idx) {
        if (!PA_SOURCE_IS_LINKED(source->state))
            continue;

        if (!best) {
            best = source;
            continue;
        }

        if (compare_sources(source, best) > 0)
            best = source;
    }

    old_default_source = core->default_source;

    if (best == old_default_source)
        return;

    core->default_source = best;
    pa_log_info("default_source: %s -> %s",
                old_default_source ? old_default_source->name : "(unset)", best ? best->name : "(unset)");
    pa_subscription_post(core, PA_SUBSCRIPTION_EVENT_SERVER | PA_SUBSCRIPTION_EVENT_CHANGE, PA_INVALID_INDEX);
    pa_hook_fire(&core->hooks[PA_CORE_HOOK_DEFAULT_SOURCE_CHANGED], core->default_source);

    /* try to move the streams from old_default_source to the new default_source conditionally */
    if (old_default_source)
	pa_source_move_streams_to_default_source(core, old_default_source, true);
}

void pa_core_set_exit_idle_time(pa_core *core, int time) {
    pa_assert(core);

    if (time == core->exit_idle_time)
        return;

    pa_log_info("exit_idle_time: %i -> %i", core->exit_idle_time, time);
    core->exit_idle_time = time;
}

static void exit_callback(pa_mainloop_api *m, pa_time_event *e, const struct timeval *t, void *userdata) {
    pa_core *c = userdata;
    pa_assert(c->exit_event == e);

    pa_log_info("We are idle, quitting...");
    pa_core_exit(c, true, 0);
}

void pa_core_check_idle(pa_core *c) {
    pa_assert(c);

    if (!c->exit_event &&
        c->exit_idle_time >= 0 &&
        pa_idxset_size(c->clients) == 0) {

        c->exit_event = pa_core_rttime_new(c, pa_rtclock_now() + c->exit_idle_time * PA_USEC_PER_SEC, exit_callback, c);

    } else if (c->exit_event && pa_idxset_size(c->clients) > 0) {
        c->mainloop->time_free(c->exit_event);
        c->exit_event = NULL;
    }
}

int pa_core_exit(pa_core *c, bool force, int retval) {
    pa_assert(c);

    if (c->disallow_exit && !force)
        return -1;

    c->mainloop->quit(c->mainloop, retval);
    return 0;
}

void pa_core_maybe_vacuum(pa_core *c) {
    pa_assert(c);

    if (pa_idxset_isempty(c->sink_inputs) && pa_idxset_isempty(c->source_outputs)) {
        pa_log_debug("Hmm, no streams around, trying to vacuum.");
    } else {
        pa_sink *si;
        pa_source *so;
        uint32_t idx;

        idx = 0;
        PA_IDXSET_FOREACH(si, c->sinks, idx)
            if (si->state != PA_SINK_SUSPENDED)
                return;

        idx = 0;
        PA_IDXSET_FOREACH(so, c->sources, idx)
            if (so->state != PA_SOURCE_SUSPENDED)
                return;

        pa_log_info("All sinks and sources are suspended, vacuuming memory");
    }

    pa_mempool_vacuum(c->mempool);
}

pa_time_event* pa_core_rttime_new(pa_core *c, pa_usec_t usec, pa_time_event_cb_t cb, void *userdata) {
    struct timeval tv;

    pa_assert(c);
    pa_assert(c->mainloop);

    return c->mainloop->time_new(c->mainloop, pa_timeval_rtstore(&tv, usec, true), cb, userdata);
}

void pa_core_rttime_restart(pa_core *c, pa_time_event *e, pa_usec_t usec) {
    struct timeval tv;

    pa_assert(c);
    pa_assert(c->mainloop);

    c->mainloop->time_restart(e, pa_timeval_rtstore(&tv, usec, true));
}

void pa_core_move_streams_to_newly_available_preferred_sink(pa_core *c, pa_sink *s) {
    pa_sink_input *si;
    uint32_t idx;

    pa_assert(c);
    pa_assert(s);

    PA_IDXSET_FOREACH(si, c->sink_inputs, idx) {
        if (si->sink == s)
            continue;

        if (!si->sink)
            continue;

        /* Skip this sink input if it is connecting a filter sink to
         * the master */
        if (si->origin_sink)
            continue;

        /* It might happen that a stream and a sink are set up at the
           same time, in which case we want to make sure we don't
           interfere with that */
        if (!PA_SINK_INPUT_IS_LINKED(si->state))
            continue;

        if (pa_safe_streq(si->preferred_sink, s->name))
            pa_sink_input_move_to(si, s, false);
    }

}

void pa_core_move_streams_to_newly_available_preferred_source(pa_core *c, pa_source *s) {
    pa_source_output *so;
    uint32_t idx;

    pa_assert(c);
    pa_assert(s);

    PA_IDXSET_FOREACH(so, c->source_outputs, idx) {
        if (so->source == s)
            continue;

        if (so->direct_on_input)
            continue;

        if (!so->source)
            continue;

        /* Skip this source output if it is connecting a filter source to
         * the master */
        if (so->destination_source)
            continue;

        /* It might happen that a stream and a source are set up at the
           same time, in which case we want to make sure we don't
           interfere with that */
        if (!PA_SOURCE_OUTPUT_IS_LINKED(so->state))
            continue;

        if (pa_safe_streq(so->preferred_source, s->name))
            pa_source_output_move_to(so, s, false);
    }

}


/* Helper macro to reduce repetition in pa_suspend_cause_to_string().
 * Parameters:
 *   char *p: the current position in the write buffer
 *   bool first: is cause_to_check the first cause to be written?
 *   pa_suspend_cause_t cause_bitfield: the causes given to pa_suspend_cause_to_string()
 *   pa_suspend_cause_t cause_to_check: the cause whose presence in cause_bitfield is to be checked
 */
#define CHECK_CAUSE(p, first, cause_bitfield, cause_to_check) \
    if (cause_bitfield & PA_SUSPEND_##cause_to_check) {       \
        size_t len = sizeof(#cause_to_check) - 1;             \
        if (!first) {                                         \
            *p = '|';                                         \
            p++;                                              \
        }                                                     \
        first = false;                                        \
        memcpy(p, #cause_to_check, len);                      \
        p += len;                                             \
    }

const char *pa_suspend_cause_to_string(pa_suspend_cause_t cause_bitfield, char buf[PA_SUSPEND_CAUSE_TO_STRING_BUF_SIZE]) {
    char *p = buf;
    bool first = true;

    CHECK_CAUSE(p, first, cause_bitfield, USER);
    CHECK_CAUSE(p, first, cause_bitfield, APPLICATION);
    CHECK_CAUSE(p, first, cause_bitfield, IDLE);
    CHECK_CAUSE(p, first, cause_bitfield, SESSION);
    CHECK_CAUSE(p, first, cause_bitfield, PASSTHROUGH);
    CHECK_CAUSE(p, first, cause_bitfield, INTERNAL);
    CHECK_CAUSE(p, first, cause_bitfield, UNAVAILABLE);

    if (p == buf) {
        memcpy(p, "(none)", 6);
        p += 6;
    }

    *p = 0;

    return buf;
}
