#ifndef foopulsesinkinputhfoo
#define foopulsesinkinputhfoo

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

#include <inttypes.h>

#include <pulsecore/typedefs.h>
#include <pulse/sample.h>
#include <pulse/format.h>
#include <pulsecore/memblockq.h>
#include <pulsecore/resampler.h>
#include <pulsecore/module.h>
#include <pulsecore/client.h>
#include <pulsecore/sink.h>
#include <pulsecore/core.h>

typedef enum pa_sink_input_state {
    PA_SINK_INPUT_INIT,         /*< The stream is not active yet, because pa_sink_input_put() has not been called yet */
    PA_SINK_INPUT_RUNNING,      /*< The stream is alive and kicking */
    PA_SINK_INPUT_CORKED,       /*< The stream was corked on user request */
    PA_SINK_INPUT_UNLINKED      /*< The stream is dead */
    /* FIXME: we need a state for MOVING here */
} pa_sink_input_state_t;

static inline bool PA_SINK_INPUT_IS_LINKED(pa_sink_input_state_t x) {
    return x == PA_SINK_INPUT_RUNNING || x == PA_SINK_INPUT_CORKED;
}

typedef enum pa_sink_input_flags {
    PA_SINK_INPUT_VARIABLE_RATE = 1,
    PA_SINK_INPUT_DONT_MOVE = 2,
    PA_SINK_INPUT_START_CORKED = 4,
    PA_SINK_INPUT_NO_REMAP = 8,
    PA_SINK_INPUT_NO_REMIX = 16,
    PA_SINK_INPUT_FIX_FORMAT = 32,
    PA_SINK_INPUT_FIX_RATE = 64,
    PA_SINK_INPUT_FIX_CHANNELS = 128,
    PA_SINK_INPUT_DONT_INHIBIT_AUTO_SUSPEND = 256,
    PA_SINK_INPUT_NO_CREATE_ON_SUSPEND = 512,
    PA_SINK_INPUT_KILL_ON_SUSPEND = 1024,
    PA_SINK_INPUT_PASSTHROUGH = 2048
} pa_sink_input_flags_t;

struct pa_sink_input {
    pa_msgobject parent;

    uint32_t index;
    pa_core *core;

    pa_sink_input_state_t state;
    pa_sink_input_flags_t flags;

    char *driver;                       /* may be NULL */
    pa_proplist *proplist;

    pa_module *module;                  /* may be NULL */
    pa_client *client;                  /* may be NULL */

    pa_sink *sink;                      /* NULL while we are being moved */

    /* This is set to true when creating the sink input if the sink was
     * requested by the application that created the sink input. This is
     * sometimes useful for determining whether the sink input should be
     * moved by some automatic policy. If the sink input is moved away from the
     * sink that the application requested, this flag is reset to false. */
    bool sink_requested_by_application;

    pa_sink *origin_sink;               /* only set by filter sinks */

    /* A sink input may be connected to multiple source outputs
     * directly, so that they don't get mixed data of the entire
     * source. */
    pa_idxset *direct_outputs;

    pa_sample_spec sample_spec;
    pa_channel_map channel_map;
    pa_format_info *format;

    pa_sink_input *sync_prev, *sync_next;

    /* Also see http://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/Developer/Volumes/ */
    pa_cvolume volume;             /* The volume clients are informed about */
    pa_cvolume reference_ratio;    /* The ratio of the stream's volume to the sink's reference volume */
    pa_cvolume real_ratio;         /* The ratio of the stream's volume to the sink's real volume */
    /* volume_factor is an internally used "additional volume" that can be used
     * by modules without having the volume visible to clients. volume_factor
     * calculated by merging all the individual items in volume_factor_items.
     * Modules must not modify these variables directly, instead
     * pa_sink_input_add/remove_volume_factor() have to be used to add and
     * remove items, or pa_sink_input_new_data_add_volume_factor() during input
     * creation time. */
    pa_cvolume volume_factor;
    pa_hashmap *volume_factor_items;
    pa_cvolume soft_volume;          /* The internal software volume we apply to all PCM data while it passes through. Usually calculated as real_ratio * volume_factor */

    pa_cvolume volume_factor_sink; /* A second volume factor in format of the sink this stream is connected to. */
    pa_hashmap *volume_factor_sink_items;

    bool volume_writable:1;

    bool muted:1;

    /* if true then the volume and the mute state of this sink-input
     * are worth remembering, module-stream-restore looks for
     * this.*/
    bool save_volume:1, save_muted:1;

    /* if users move the sink-input to a sink, and the sink is not default_sink,
     * the sink->name will be saved in preferred_sink. And later if sink-input
     * is moved to other sinks for some reason, it still can be restored to the
     * preferred_sink at an appropriate time */
    char *preferred_sink;

    pa_resample_method_t requested_resample_method, actual_resample_method;

    /* Returns the chunk of audio data and drops it from the
     * queue. Returns -1 on failure. Called from IO thread context. If
     * data needs to be generated from scratch then please in the
     * specified length request_nbytes. This is an optimization
     * only. If less data is available, it's fine to return a smaller
     * block. If more data is already ready, it is better to return
     * the full block. */
    int (*pop) (pa_sink_input *i, size_t request_nbytes, pa_memchunk *chunk); /* may NOT be NULL */

    /* This is called when the playback buffer has actually played back
       all available data. Return true unless there is more data to play back.
       Called from IO context. */
    bool (*process_underrun) (pa_sink_input *i);

    /* Rewind the queue by the specified number of bytes. Called just
     * before peek() if it is called at all. Only called if the sink
     * input driver ever plans to call
     * pa_sink_input_request_rewind(). Called from IO context. */
    void (*process_rewind) (pa_sink_input *i, size_t nbytes);     /* may NOT be NULL */

    /* Called whenever the maximum rewindable size of the sink
     * changes. Called from IO context. */
    void (*update_max_rewind) (pa_sink_input *i, size_t nbytes); /* may be NULL */

    /* Called whenever the maximum request size of the sink
     * changes. Called from IO context. */
    void (*update_max_request) (pa_sink_input *i, size_t nbytes); /* may be NULL */

    /* Called whenever the configured latency of the sink
     * changes. Called from IO context. */
    void (*update_sink_requested_latency) (pa_sink_input *i); /* may be NULL */

    /* Called whenever the latency range of the sink changes. Called
     * from IO context. */
    void (*update_sink_latency_range) (pa_sink_input *i); /* may be NULL */

    /* Called whenever the fixed latency of the sink changes, if there
     * is one. Called from IO context. */
    void (*update_sink_fixed_latency) (pa_sink_input *i); /* may be NULL */

    /* If non-NULL this function is called when the input is first
     * connected to a sink or when the rtpoll/asyncmsgq fields
     * change. You usually don't need to implement this function
     * unless you rewrite a sink that is piggy-backed onto
     * another. Called from IO thread context */
    void (*attach) (pa_sink_input *i);           /* may be NULL */

    /* If non-NULL this function is called when the output is
     * disconnected from its sink. Called from IO thread context */
    void (*detach) (pa_sink_input *i);           /* may be NULL */

    /* If non-NULL called whenever the sink this input is attached
     * to suspends or resumes or if the suspend cause changes.
     * Called from main context */
    void (*suspend) (pa_sink_input *i, pa_sink_state_t old_state, pa_suspend_cause_t old_suspend_cause);   /* may be NULL */

    /* If non-NULL called whenever the sink this input is attached
     * to suspends or resumes. Called from IO context */
    void (*suspend_within_thread) (pa_sink_input *i, bool b);   /* may be NULL */

    /* If non-NULL called whenever the sink input is moved to a new
     * sink. Called from main context after the sink input has been
     * detached from the old sink and before it has been attached to
     * the new sink. If dest is NULL the move was executed in two
     * phases and the second one failed; the stream will be destroyed
     * after this call. */
    void (*moving) (pa_sink_input *i, pa_sink *dest);   /* may be NULL */

    /* Supposed to unlink and destroy this stream. Called from main
     * context. */
    void (*kill) (pa_sink_input *i);             /* may NOT be NULL */

    /* Return the current latency (i.e. length of buffered audio) of
    this stream. Called from main context. This is added to what the
    PA_SINK_INPUT_MESSAGE_GET_LATENCY message sent to the IO thread
    returns */
    pa_usec_t (*get_latency) (pa_sink_input *i); /* may be NULL */

    /* If non-NULL this function is called from thread context if the
     * state changes. The old state is found in thread_info.state.  */
    void (*state_change) (pa_sink_input *i, pa_sink_input_state_t state); /* may be NULL */

    /* If non-NULL this function is called before this sink input is
     * move to a sink and if it returns false the move will not
     * be allowed */
    bool (*may_move_to) (pa_sink_input *i, pa_sink *s); /* may be NULL */

    /* If non-NULL this function is used to dispatch asynchronous
     * control events. Called from main context. */
    void (*send_event)(pa_sink_input *i, const char *event, pa_proplist* data); /* may be NULL */

    /* If non-NULL this function is called whenever the sink input
     * volume changes. Called from main context */
    void (*volume_changed)(pa_sink_input *i); /* may be NULL */

    /* If non-NULL this function is called whenever the sink input
     * mute status changes. Called from main context */
    void (*mute_changed)(pa_sink_input *i); /* may be NULL */

    struct {
        pa_sink_input_state_t state;

        pa_cvolume soft_volume;
        bool muted:1;

        bool attached:1; /* True only between ->attach() and ->detach() calls */

        /* rewrite_nbytes: 0: rewrite nothing, (size_t) -1: rewrite everything, otherwise how many bytes to rewrite */
        bool rewrite_flush:1, dont_rewind_render:1;
        size_t rewrite_nbytes;
        uint64_t underrun_for, playing_for;
        uint64_t underrun_for_sink; /* Like underrun_for, but in sink sample spec */

        pa_sample_spec sample_spec;

        pa_resampler *resampler;                     /* may be NULL */

        /* We maintain a history of resampled audio data here. */
        pa_memblockq *render_memblockq;

        pa_sink_input *sync_prev, *sync_next;

        /* The requested latency for the sink */
        pa_usec_t requested_sink_latency;

        pa_hashmap *direct_outputs;
    } thread_info;

    void *userdata;
};

PA_DECLARE_PUBLIC_CLASS(pa_sink_input);
#define PA_SINK_INPUT(o) pa_sink_input_cast(o)

enum {
    PA_SINK_INPUT_MESSAGE_SET_SOFT_VOLUME,
    PA_SINK_INPUT_MESSAGE_SET_SOFT_MUTE,
    PA_SINK_INPUT_MESSAGE_GET_LATENCY,
    PA_SINK_INPUT_MESSAGE_SET_RATE,
    PA_SINK_INPUT_MESSAGE_SET_STATE,
    PA_SINK_INPUT_MESSAGE_SET_REQUESTED_LATENCY,
    PA_SINK_INPUT_MESSAGE_GET_REQUESTED_LATENCY,
    PA_SINK_INPUT_MESSAGE_MAX
};

typedef struct pa_sink_input_send_event_hook_data {
    pa_sink_input *sink_input;
    const char *event;
    pa_proplist *data;
} pa_sink_input_send_event_hook_data;

typedef struct pa_sink_input_new_data {
    pa_sink_input_flags_t flags;

    pa_proplist *proplist;

    const char *driver;
    pa_module *module;
    pa_client *client;

    pa_sink *sink;
    bool sink_requested_by_application;
    pa_sink *origin_sink;

    pa_resample_method_t resample_method;

    pa_sink_input *sync_base;

    pa_sample_spec sample_spec;
    pa_channel_map channel_map;
    pa_format_info *format;
    pa_idxset *req_formats;
    pa_idxset *nego_formats;

    pa_cvolume volume;
    bool muted:1;
    pa_hashmap *volume_factor_items, *volume_factor_sink_items;

    bool sample_spec_is_set:1;
    bool channel_map_is_set:1;

    bool volume_is_set:1;
    bool muted_is_set:1;

    bool volume_is_absolute:1;

    bool volume_writable:1;

    bool save_volume:1, save_muted:1;

    char *preferred_sink;
} pa_sink_input_new_data;

pa_sink_input_new_data* pa_sink_input_new_data_init(pa_sink_input_new_data *data);
void pa_sink_input_new_data_set_sample_spec(pa_sink_input_new_data *data, const pa_sample_spec *spec);
void pa_sink_input_new_data_set_channel_map(pa_sink_input_new_data *data, const pa_channel_map *map);
bool pa_sink_input_new_data_is_passthrough(pa_sink_input_new_data *data);
void pa_sink_input_new_data_set_volume(pa_sink_input_new_data *data, const pa_cvolume *volume);
void pa_sink_input_new_data_add_volume_factor(pa_sink_input_new_data *data, const char *key, const pa_cvolume *volume_factor);
void pa_sink_input_new_data_add_volume_factor_sink(pa_sink_input_new_data *data, const char *key, const pa_cvolume *volume_factor);
void pa_sink_input_new_data_set_muted(pa_sink_input_new_data *data, bool mute);
bool pa_sink_input_new_data_set_sink(pa_sink_input_new_data *data, pa_sink *s, bool save, bool requested_by_application);
bool pa_sink_input_new_data_set_formats(pa_sink_input_new_data *data, pa_idxset *formats);
void pa_sink_input_new_data_done(pa_sink_input_new_data *data);

/* To be called by the implementing module only */

int pa_sink_input_new(
        pa_sink_input **i,
        pa_core *core,
        pa_sink_input_new_data *data);

void pa_sink_input_put(pa_sink_input *i);
void pa_sink_input_unlink(pa_sink_input* i);

pa_usec_t pa_sink_input_set_requested_latency(pa_sink_input *i, pa_usec_t usec);

/* Request that the specified number of bytes already written out to
the hw device is rewritten, if possible.  Please note that this is
only a kind request. The sink driver may not be able to fulfill it
fully -- or at all. If the request for a rewrite was successful, the
sink driver will call ->rewind() and pass the number of bytes that
could be rewound in the HW device. This functionality is required for
implementing the "zero latency" write-through functionality. */
void pa_sink_input_request_rewind(pa_sink_input *i, size_t nbytes, bool rewrite, bool flush, bool dont_rewind_render);

void pa_sink_input_cork(pa_sink_input *i, bool b);

int pa_sink_input_set_rate(pa_sink_input *i, uint32_t rate);
int pa_sink_input_update_resampler(pa_sink_input *i);

/* This returns the sink's fields converted into out sample type */
size_t pa_sink_input_get_max_rewind(pa_sink_input *i);
size_t pa_sink_input_get_max_request(pa_sink_input *i);

/* Callable by everyone from main thread*/

/* External code may request disconnection with this function */
void pa_sink_input_kill(pa_sink_input*i);

pa_usec_t pa_sink_input_get_latency(pa_sink_input *i, pa_usec_t *sink_latency);

bool pa_sink_input_is_passthrough(pa_sink_input *i);
bool pa_sink_input_is_volume_readable(pa_sink_input *i);
void pa_sink_input_set_volume(pa_sink_input *i, const pa_cvolume *volume, bool save, bool absolute);
void pa_sink_input_add_volume_factor(pa_sink_input *i, const char *key, const pa_cvolume *volume_factor);
int pa_sink_input_remove_volume_factor(pa_sink_input *i, const char *key);
pa_cvolume *pa_sink_input_get_volume(pa_sink_input *i, pa_cvolume *volume, bool absolute);

void pa_sink_input_set_mute(pa_sink_input *i, bool mute, bool save);

void pa_sink_input_set_property(pa_sink_input *i, const char *key, const char *value);
void pa_sink_input_set_property_arbitrary(pa_sink_input *i, const char *key, const uint8_t *value, size_t nbytes);
void pa_sink_input_update_proplist(pa_sink_input *i, pa_update_mode_t mode, pa_proplist *p);

pa_resample_method_t pa_sink_input_get_resample_method(pa_sink_input *i);

void pa_sink_input_send_event(pa_sink_input *i, const char *name, pa_proplist *data);

int pa_sink_input_move_to(pa_sink_input *i, pa_sink *dest, bool save);
bool pa_sink_input_may_move(pa_sink_input *i); /* may this sink input move at all? */
bool pa_sink_input_may_move_to(pa_sink_input *i, pa_sink *dest); /* may this sink input move to this sink? */

/* The same as pa_sink_input_move_to() but in two separate steps,
 * first the detaching from the old sink, then the attaching to the
 * new sink */
int pa_sink_input_start_move(pa_sink_input *i);
int pa_sink_input_finish_move(pa_sink_input *i, pa_sink *dest, bool save);
void pa_sink_input_fail_move(pa_sink_input *i);

pa_usec_t pa_sink_input_get_requested_latency(pa_sink_input *i);

/* To be used exclusively by the sink driver IO thread */

void pa_sink_input_peek(pa_sink_input *i, size_t length, pa_memchunk *chunk, pa_cvolume *volume);
void pa_sink_input_drop(pa_sink_input *i, size_t length);
void pa_sink_input_process_rewind(pa_sink_input *i, size_t nbytes /* in the sink's sample spec */);
void pa_sink_input_update_max_rewind(pa_sink_input *i, size_t nbytes  /* in the sink's sample spec */);
void pa_sink_input_update_max_request(pa_sink_input *i, size_t nbytes  /* in the sink's sample spec */);

void pa_sink_input_set_state_within_thread(pa_sink_input *i, pa_sink_input_state_t state);

int pa_sink_input_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk);

pa_usec_t pa_sink_input_set_requested_latency_within_thread(pa_sink_input *i, pa_usec_t usec);

bool pa_sink_input_safe_to_remove(pa_sink_input *i);
bool pa_sink_input_process_underrun(pa_sink_input *i);

pa_memchunk* pa_sink_input_get_silence(pa_sink_input *i, pa_memchunk *ret);

/* Calls the attach() callback if it's set. The input must be in detached
 * state. */
void pa_sink_input_attach(pa_sink_input *i);

/* Calls the detach() callback if it's set and the input is attached. The input
 * is allowed to be already detached, in which case this does nothing.
 *
 * The reason why this can be called for already-detached inputs is that when
 * a filter sink's input is detached, it has to detach also all inputs
 * connected to the filter sink. In case the filter sink's input was detached
 * because the filter sink is being removed, those other inputs will be moved
 * to another sink or removed, and moving and removing involve detaching the
 * inputs, but the inputs at that point are already detached.
 *
 * XXX: Moving or removing an input also involves sending messages to the
 * input's sink. If the input's sink is a detached filter sink, shouldn't
 * sending messages to it be prohibited? The messages are processed in the
 * root sink's IO thread, and when the filter sink is detached, it would seem
 * logical to prohibit any interaction with the IO thread that isn't any more
 * associated with the filter sink. Currently sending messages to detached
 * filter sinks mostly works, because the filter sinks don't update their
 * asyncmsgq pointer when detaching, so messages still find their way to the
 * old IO thread. */
void pa_sink_input_detach(pa_sink_input *i);

/* Called from the main thread, from sink.c only. The normal way to set the
 * sink input volume is to call pa_sink_input_set_volume(), but the flat volume
 * logic in sink.c needs also a function that doesn't do all the extra stuff
 * that pa_sink_input_set_volume() does. This function simply sets i->volume
 * and fires change notifications. */
void pa_sink_input_set_volume_direct(pa_sink_input *i, const pa_cvolume *volume);

/* Called from the main thread, from sink.c only. This shouldn't be a public
 * function, but the flat volume logic in sink.c currently needs a way to
 * directly set the sink input reference ratio. This function simply sets
 * i->reference_ratio and logs a message if the value changes. */
void pa_sink_input_set_reference_ratio(pa_sink_input *i, const pa_cvolume *ratio);

void pa_sink_input_set_preferred_sink(pa_sink_input *i, pa_sink *s);

#define pa_sink_input_assert_io_context(s) \
    pa_assert(pa_thread_mq_get() || !PA_SINK_INPUT_IS_LINKED((s)->state))

#endif
