#ifndef foopulsesinkhfoo
#define foopulsesinkhfoo

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
#include <pulse/def.h>
#include <pulse/format.h>
#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulse/volume.h>

#include <pulsecore/core.h>
#include <pulsecore/idxset.h>
#include <pulsecore/memchunk.h>
#include <pulsecore/source.h>
#include <pulsecore/module.h>
#include <pulsecore/asyncmsgq.h>
#include <pulsecore/msgobject.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/device-port.h>
#include <pulsecore/card.h>
#include <pulsecore/queue.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/sink-input.h>

#define PA_MAX_INPUTS_PER_SINK 256

/* Returns true if sink is linked: registered and accessible from client side. */
static inline bool PA_SINK_IS_LINKED(pa_sink_state_t x) {
    return x == PA_SINK_RUNNING || x == PA_SINK_IDLE || x == PA_SINK_SUSPENDED;
}

/* A generic definition for void callback functions */
typedef void(*pa_sink_cb_t)(pa_sink *s);

typedef int (*pa_sink_get_mute_cb_t)(pa_sink *s, bool *mute);

struct pa_sink {
    pa_msgobject parent;

    uint32_t index;
    pa_core *core;

    pa_sink_state_t state;

    /* Set in the beginning of pa_sink_unlink() before setting the sink state
     * to UNLINKED. The purpose is to prevent moving streams to a sink that is
     * about to be removed. */
    bool unlink_requested;

    pa_sink_flags_t flags;
    pa_suspend_cause_t suspend_cause;

    char *name;
    char *driver;                           /* may be NULL */
    pa_proplist *proplist;

    pa_module *module;                      /* may be NULL */
    pa_card *card;                          /* may be NULL */

    pa_sample_spec sample_spec;
    pa_channel_map channel_map;
    uint32_t default_sample_rate;
    uint32_t alternate_sample_rate;
    bool avoid_resampling:1;

    pa_idxset *inputs;
    unsigned n_corked;
    pa_source *monitor_source;
    pa_sink_input *input_to_master;         /* non-NULL only for filter sinks */

    pa_volume_t base_volume; /* shall be constant */
    unsigned n_volume_steps; /* shall be constant */

    /* Also see http://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/Developer/Volumes/ */
    pa_cvolume reference_volume; /* The volume exported and taken as reference base for relative sink input volumes */
    pa_cvolume real_volume;      /* The volume that the hardware is configured to  */
    pa_cvolume soft_volume;      /* The internal software volume we apply to all PCM data while it passes through */

    bool muted:1;

    bool refresh_volume:1;
    bool refresh_muted:1;
    bool save_port:1;
    bool save_volume:1;
    bool save_muted:1;

    /* Saved volume state while we're in passthrough mode */
    pa_cvolume saved_volume;
    bool saved_save_volume:1;

    pa_asyncmsgq *asyncmsgq;

    pa_memchunk silence;

    pa_hashmap *ports;
    pa_device_port *active_port;

    /* The latency offset is inherited from the currently active port */
    int64_t port_latency_offset;

    unsigned priority;

    bool set_mute_in_progress;

    /* Callbacks for doing things when the sink state and/or suspend cause is
     * changed. It's fine to set either or both of the callbacks to NULL if the
     * implementation doesn't have anything to do on state or suspend cause
     * changes.
     *
     * set_state_in_main_thread() is called first. The callback is allowed to
     * report failure if and only if the sink changes its state from
     * SUSPENDED to IDLE or RUNNING. (FIXME: It would make sense to allow
     * failure also when changing state from INIT to IDLE or RUNNING, but
     * currently that will crash pa_sink_put().) If
     * set_state_in_main_thread() fails, set_state_in_io_thread() won't be
     * called.
     *
     * If set_state_in_main_thread() is successful (or not set), then
     * set_state_in_io_thread() is called. Again, failure is allowed if and
     * only if the sink changes state from SUSPENDED to IDLE or RUNNING. If
     * set_state_in_io_thread() fails, then set_state_in_main_thread() is
     * called again, this time with the state parameter set to SUSPENDED and
     * the suspend_cause parameter set to 0.
     *
     * pa_sink.state, pa_sink.thread_info.state and pa_sink.suspend_cause
     * are updated only after all the callback calls. In case of failure, the
     * state is set to SUSPENDED and the suspend cause is set to 0. */
    int (*set_state_in_main_thread)(pa_sink *s, pa_sink_state_t state, pa_suspend_cause_t suspend_cause); /* may be NULL */
    int (*set_state_in_io_thread)(pa_sink *s, pa_sink_state_t state, pa_suspend_cause_t suspend_cause); /* may be NULL */

    /* Sink drivers that support hardware volume may set this
     * callback. This is called when the current volume needs to be
     * re-read from the hardware.
     *
     * There are two ways for drivers to implement hardware volume
     * query: either set this callback or handle
     * PA_SINK_MESSAGE_GET_VOLUME. The callback implementation or the
     * message handler must update s->real_volume and s->soft_volume
     * (using pa_sink_set_soft_volume()) to match the current hardware
     * volume.
     *
     * If PA_SINK_DEFERRED_VOLUME is not set, then this is called from the
     * main thread before sending PA_SINK_MESSAGE_GET_VOLUME, so in
     * this case the driver can choose whether to read the volume from
     * the hardware in the main thread or in the IO thread.
     *
     * If PA_SINK_DEFERRED_VOLUME is set, then this is called from the IO
     * thread within the default handler for
     * PA_SINK_MESSAGE_GET_VOLUME (the main thread is waiting while
     * the message is being processed), so there's no choice of where
     * to do the volume reading - it has to be done in the IO thread
     * always.
     *
     * You must use the function pa_sink_set_get_volume_callback() to
     * set this callback. */
    pa_sink_cb_t get_volume; /* may be NULL */

    /* Sink drivers that support hardware volume must set this
     * callback. This is called when the hardware volume needs to be
     * updated.
     *
     * If PA_SINK_DEFERRED_VOLUME is not set, then this is called from the
     * main thread. The callback implementation must set the hardware
     * volume according to s->real_volume. If the driver can't set the
     * hardware volume to the exact requested value, it has to update
     * s->real_volume and/or s->soft_volume so that they together
     * match the actual hardware volume that was set.
     *
     * If PA_SINK_DEFERRED_VOLUME is set, then this is called from the IO
     * thread. The callback implementation must not actually set the
     * hardware volume yet, but it must check how close to the
     * requested volume the hardware volume can be set, and update
     * s->real_volume and/or s->soft_volume so that they together
     * match the actual hardware volume that will be set later in the
     * write_volume callback.
     *
     * You must use the function pa_sink_set_set_volume_callback() to
     * set this callback. */
    pa_sink_cb_t set_volume; /* may be NULL */

    /* Sink drivers that set PA_SINK_DEFERRED_VOLUME must provide this
     * callback. This callback is not used with sinks that do not set
     * PA_SINK_DEFERRED_VOLUME. This is called from the IO thread when a
     * pending hardware volume change has to be written to the
     * hardware. The requested volume is passed to the callback
     * implementation in s->thread_info.current_hw_volume.
     *
     * The call is done inside pa_sink_volume_change_apply(), which is
     * not called automatically - it is the driver's responsibility to
     * schedule that function to be called at the right times in the
     * IO thread.
     *
     * You must use the function pa_sink_set_write_volume_callback() to
     * set this callback. */
    pa_sink_cb_t write_volume; /* may be NULL */

    /* If the sink mute can change "spontaneously" (i.e. initiated by the sink
     * implementation, not by someone else calling pa_sink_set_mute()), then
     * the sink implementation can notify about changed mute either by calling
     * pa_sink_mute_changed() or by calling pa_sink_get_mute() with
     * force_refresh=true. If the implementation chooses the latter approach,
     * it should implement the get_mute callback. Otherwise get_mute can be
     * NULL.
     *
     * This is called when pa_sink_get_mute() is called with
     * force_refresh=true. This is called from the IO thread if the
     * PA_SINK_DEFERRED_VOLUME flag is set, otherwise this is called from the
     * main thread. On success, the implementation is expected to return 0 and
     * set the mute parameter that is passed as a reference. On failure, the
     * implementation is expected to return -1.
     *
     * You must use the function pa_sink_set_get_mute_callback() to
     * set this callback. */
    pa_sink_get_mute_cb_t get_mute;

    /* Called when the mute setting shall be changed. A PA_SINK_MESSAGE_SET_MUTE
     * message will also be sent. Called from IO thread if PA_SINK_DEFERRED_VOLUME
     * flag is set otherwise from main loop context.
     *
     * You must use the function pa_sink_set_set_mute_callback() to
     * set this callback. */
    pa_sink_cb_t set_mute; /* may be NULL */

    /* Called when a rewind request is issued. Called from IO thread
     * context. */
    pa_sink_cb_t request_rewind; /* may be NULL */

    /* Called when a the requested latency is changed. Called from IO
     * thread context. */
    pa_sink_cb_t update_requested_latency; /* may be NULL */

    /* Called whenever the port shall be changed. Called from the main
     * thread. */
    int (*set_port)(pa_sink *s, pa_device_port *port); /* may be NULL */

    /* Called to get the list of formats supported by the sink, sorted
     * in descending order of preference. */
    pa_idxset* (*get_formats)(pa_sink *s); /* may be NULL */

    /* Called to set the list of formats supported by the sink. Can be
     * NULL if the sink does not support this. Returns true on success,
     * false otherwise (for example when an unsupportable format is
     * set). Makes a copy of the formats passed in. */
    bool (*set_formats)(pa_sink *s, pa_idxset *formats); /* may be NULL */

    /* Called whenever device parameters need to be changed. Called from
     * main thread. */
    void (*reconfigure)(pa_sink *s, pa_sample_spec *spec, bool passthrough);

    /* Contains copies of the above data so that the real-time worker
     * thread can work without access locking */
    struct {
        pa_sink_state_t state;
        pa_hashmap *inputs;

        pa_rtpoll *rtpoll;

        pa_cvolume soft_volume;
        bool soft_muted:1;

        /* The requested latency is used for dynamic latency
         * sinks. For fixed latency sinks it is always identical to
         * the fixed_latency. See below. */
        bool requested_latency_valid:1;
        pa_usec_t requested_latency;

        /* The number of bytes streams need to keep around as history to
         * be able to satisfy every DMA buffer rewrite */
        size_t max_rewind;

        /* The number of bytes streams need to keep around to satisfy
         * every DMA write request */
        size_t max_request;

        /* Maximum of what clients requested to rewind in this cycle */
        size_t rewind_nbytes;
        bool rewind_requested;

        /* Both dynamic and fixed latencies will be clamped to this
         * range. */
        pa_usec_t min_latency; /* we won't go below this latency */
        pa_usec_t max_latency; /* An upper limit for the latencies */

        /* 'Fixed' simply means that the latency is exclusively
         * decided on by the sink, and the clients have no influence
         * in changing it */
        pa_usec_t fixed_latency; /* for sinks with PA_SINK_DYNAMIC_LATENCY this is 0 */

        /* This latency offset is a direct copy from s->port_latency_offset */
        int64_t port_latency_offset;

        /* Delayed volume change events are queued here. The events
         * are stored in expiration order. The one expiring next is in
         * the head of the list. */
        PA_LLIST_HEAD(pa_sink_volume_change, volume_changes);
        pa_sink_volume_change *volume_changes_tail;
        /* This value is updated in pa_sink_volume_change_apply() and
         * used only by sinks with PA_SINK_DEFERRED_VOLUME. */
        pa_cvolume current_hw_volume;

        /* The amount of usec volume up events are delayed and volume
         * down events are made earlier. */
        uint32_t volume_change_safety_margin;
        /* Usec delay added to all volume change events, may be negative. */
        int32_t volume_change_extra_delay;
    } thread_info;

    void *userdata;
};

PA_DECLARE_PUBLIC_CLASS(pa_sink);
#define PA_SINK(s) (pa_sink_cast(s))

typedef enum pa_sink_message {
    PA_SINK_MESSAGE_ADD_INPUT,
    PA_SINK_MESSAGE_REMOVE_INPUT,
    PA_SINK_MESSAGE_GET_VOLUME,
    PA_SINK_MESSAGE_SET_SHARED_VOLUME,
    PA_SINK_MESSAGE_SET_VOLUME_SYNCED,
    PA_SINK_MESSAGE_SET_VOLUME,
    PA_SINK_MESSAGE_SYNC_VOLUMES,
    PA_SINK_MESSAGE_GET_MUTE,
    PA_SINK_MESSAGE_SET_MUTE,
    PA_SINK_MESSAGE_GET_LATENCY,
    PA_SINK_MESSAGE_GET_REQUESTED_LATENCY,
    PA_SINK_MESSAGE_SET_STATE,
    PA_SINK_MESSAGE_START_MOVE,
    PA_SINK_MESSAGE_FINISH_MOVE,
    PA_SINK_MESSAGE_SET_LATENCY_RANGE,
    PA_SINK_MESSAGE_GET_LATENCY_RANGE,
    PA_SINK_MESSAGE_SET_FIXED_LATENCY,
    PA_SINK_MESSAGE_GET_FIXED_LATENCY,
    PA_SINK_MESSAGE_GET_MAX_REWIND,
    PA_SINK_MESSAGE_GET_MAX_REQUEST,
    PA_SINK_MESSAGE_SET_MAX_REWIND,
    PA_SINK_MESSAGE_SET_MAX_REQUEST,
    PA_SINK_MESSAGE_UPDATE_VOLUME_AND_MUTE,
    PA_SINK_MESSAGE_SET_PORT_LATENCY_OFFSET,
    PA_SINK_MESSAGE_MAX
} pa_sink_message_t;

typedef struct pa_sink_new_data {
    pa_suspend_cause_t suspend_cause;

    char *name;
    pa_proplist *proplist;

    const char *driver;
    pa_module *module;
    pa_card *card;

    pa_hashmap *ports;
    char *active_port;

    pa_sample_spec sample_spec;
    pa_channel_map channel_map;
    uint32_t alternate_sample_rate;
    bool avoid_resampling:1;
    pa_cvolume volume;
    bool muted:1;

    bool sample_spec_is_set:1;
    bool channel_map_is_set:1;
    bool alternate_sample_rate_is_set:1;
    bool avoid_resampling_is_set:1;
    bool volume_is_set:1;
    bool muted_is_set:1;

    bool namereg_fail:1;

    bool save_port:1;
    bool save_volume:1;
    bool save_muted:1;
} pa_sink_new_data;

pa_sink_new_data* pa_sink_new_data_init(pa_sink_new_data *data);
void pa_sink_new_data_set_name(pa_sink_new_data *data, const char *name);
void pa_sink_new_data_set_sample_spec(pa_sink_new_data *data, const pa_sample_spec *spec);
void pa_sink_new_data_set_channel_map(pa_sink_new_data *data, const pa_channel_map *map);
void pa_sink_new_data_set_alternate_sample_rate(pa_sink_new_data *data, const uint32_t alternate_sample_rate);
void pa_sink_new_data_set_avoid_resampling(pa_sink_new_data *data, bool avoid_resampling);
void pa_sink_new_data_set_volume(pa_sink_new_data *data, const pa_cvolume *volume);
void pa_sink_new_data_set_muted(pa_sink_new_data *data, bool mute);
void pa_sink_new_data_set_port(pa_sink_new_data *data, const char *port);
void pa_sink_new_data_done(pa_sink_new_data *data);

/*** To be called exclusively by the sink driver, from main context */

pa_sink* pa_sink_new(
        pa_core *core,
        pa_sink_new_data *data,
        pa_sink_flags_t flags);

void pa_sink_set_get_volume_callback(pa_sink *s, pa_sink_cb_t cb);
void pa_sink_set_set_volume_callback(pa_sink *s, pa_sink_cb_t cb);
void pa_sink_set_write_volume_callback(pa_sink *s, pa_sink_cb_t cb);
void pa_sink_set_get_mute_callback(pa_sink *s, pa_sink_get_mute_cb_t cb);
void pa_sink_set_set_mute_callback(pa_sink *s, pa_sink_cb_t cb);
void pa_sink_enable_decibel_volume(pa_sink *s, bool enable);

void pa_sink_put(pa_sink *s);
void pa_sink_unlink(pa_sink* s);

void pa_sink_set_description(pa_sink *s, const char *description);
void pa_sink_set_asyncmsgq(pa_sink *s, pa_asyncmsgq *q);
void pa_sink_set_rtpoll(pa_sink *s, pa_rtpoll *p);

void pa_sink_set_max_rewind(pa_sink *s, size_t max_rewind);
void pa_sink_set_max_request(pa_sink *s, size_t max_request);
void pa_sink_set_latency_range(pa_sink *s, pa_usec_t min_latency, pa_usec_t max_latency);
void pa_sink_set_fixed_latency(pa_sink *s, pa_usec_t latency);

void pa_sink_set_soft_volume(pa_sink *s, const pa_cvolume *volume);
void pa_sink_volume_changed(pa_sink *s, const pa_cvolume *new_volume);
void pa_sink_mute_changed(pa_sink *s, bool new_muted);

void pa_sink_update_flags(pa_sink *s, pa_sink_flags_t mask, pa_sink_flags_t value);

bool pa_device_init_description(pa_proplist *p, pa_card *card);
bool pa_device_init_icon(pa_proplist *p, bool is_sink);
bool pa_device_init_intended_roles(pa_proplist *p);
unsigned pa_device_init_priority(pa_proplist *p);

/**** May be called by everyone, from main context */

void pa_sink_reconfigure(pa_sink *s, pa_sample_spec *spec, bool passthrough);
void pa_sink_set_port_latency_offset(pa_sink *s, int64_t offset);

/* The returned value is supposed to be in the time domain of the sound card! */
pa_usec_t pa_sink_get_latency(pa_sink *s);
pa_usec_t pa_sink_get_requested_latency(pa_sink *s);
void pa_sink_get_latency_range(pa_sink *s, pa_usec_t *min_latency, pa_usec_t *max_latency);
pa_usec_t pa_sink_get_fixed_latency(pa_sink *s);

size_t pa_sink_get_max_rewind(pa_sink *s);
size_t pa_sink_get_max_request(pa_sink *s);

int pa_sink_update_status(pa_sink*s);
int pa_sink_suspend(pa_sink *s, bool suspend, pa_suspend_cause_t cause);
int pa_sink_suspend_all(pa_core *c, bool suspend, pa_suspend_cause_t cause);

/* Use this instead of checking s->flags & PA_SINK_FLAT_VOLUME directly. */
bool pa_sink_flat_volume_enabled(pa_sink *s);

/* Get the master sink when sharing volumes */
pa_sink *pa_sink_get_master(pa_sink *s);

bool pa_sink_is_filter(pa_sink *s);

/* Is the sink in passthrough mode? (that is, is there a passthrough sink input
 * connected to this sink? */
bool pa_sink_is_passthrough(pa_sink *s);
/* These should be called when a sink enters/leaves passthrough mode */
void pa_sink_enter_passthrough(pa_sink *s);
void pa_sink_leave_passthrough(pa_sink *s);

void pa_sink_set_volume(pa_sink *sink, const pa_cvolume *volume, bool sendmsg, bool save);
const pa_cvolume *pa_sink_get_volume(pa_sink *sink, bool force_refresh);

void pa_sink_set_mute(pa_sink *sink, bool mute, bool save);
bool pa_sink_get_mute(pa_sink *sink, bool force_refresh);

bool pa_sink_update_proplist(pa_sink *s, pa_update_mode_t mode, pa_proplist *p);

int pa_sink_set_port(pa_sink *s, const char *name, bool save);

unsigned pa_sink_linked_by(pa_sink *s); /* Number of connected streams */
unsigned pa_sink_used_by(pa_sink *s); /* Number of connected streams which are not corked */

/* Returns how many streams are active that don't allow suspensions. If
 * "ignore_input" or "ignore_output" is non-NULL, that stream is not included
 * in the count (the returned count includes the value from
 * pa_source_check_suspend(), which is called for the monitor source, so that's
 * why "ignore_output" may be relevant). */
unsigned pa_sink_check_suspend(pa_sink *s, pa_sink_input *ignore_input, pa_source_output *ignore_output);

const char *pa_sink_state_to_string(pa_sink_state_t state);

/* Moves all inputs away, and stores them in pa_queue */
pa_queue *pa_sink_move_all_start(pa_sink *s, pa_queue *q);
void pa_sink_move_all_finish(pa_sink *s, pa_queue *q, bool save);
void pa_sink_move_all_fail(pa_queue *q);

/* Returns a copy of the sink formats. TODO: Get rid of this function (or at
 * least get rid of the copying). There's no good reason to copy the formats
 * every time someone wants to know what formats the sink supports. The formats
 * idxset could be stored directly in the pa_sink struct.
 * https://bugs.freedesktop.org/show_bug.cgi?id=71924 */
pa_idxset* pa_sink_get_formats(pa_sink *s);

bool pa_sink_set_formats(pa_sink *s, pa_idxset *formats);
bool pa_sink_check_format(pa_sink *s, pa_format_info *f);
pa_idxset* pa_sink_check_formats(pa_sink *s, pa_idxset *in_formats);

void pa_sink_set_sample_format(pa_sink *s, pa_sample_format_t format);
void pa_sink_set_sample_rate(pa_sink *s, uint32_t rate);

/*** To be called exclusively by the sink driver, from IO context */

void pa_sink_render(pa_sink*s, size_t length, pa_memchunk *result);
void pa_sink_render_full(pa_sink *s, size_t length, pa_memchunk *result);
void pa_sink_render_into(pa_sink*s, pa_memchunk *target);
void pa_sink_render_into_full(pa_sink *s, pa_memchunk *target);

void pa_sink_process_rewind(pa_sink *s, size_t nbytes);

int pa_sink_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk);

void pa_sink_attach_within_thread(pa_sink *s);
void pa_sink_detach_within_thread(pa_sink *s);

pa_usec_t pa_sink_get_requested_latency_within_thread(pa_sink *s);

void pa_sink_set_max_rewind_within_thread(pa_sink *s, size_t max_rewind);
void pa_sink_set_max_request_within_thread(pa_sink *s, size_t max_request);

void pa_sink_set_latency_range_within_thread(pa_sink *s, pa_usec_t min_latency, pa_usec_t max_latency);
void pa_sink_set_fixed_latency_within_thread(pa_sink *s, pa_usec_t latency);

void pa_sink_update_volume_and_mute(pa_sink *s);

bool pa_sink_volume_change_apply(pa_sink *s, pa_usec_t *usec_to_next);

size_t pa_sink_process_input_underruns(pa_sink *s, size_t left_to_play);

/*** To be called exclusively by sink input drivers, from IO context */

void pa_sink_request_rewind(pa_sink*s, size_t nbytes);

void pa_sink_invalidate_requested_latency(pa_sink *s, bool dynamic);

int64_t pa_sink_get_latency_within_thread(pa_sink *s, bool allow_negative);

/* Called from the main thread, from sink-input.c only. The normal way to set
 * the sink reference volume is to call pa_sink_set_volume(), but the flat
 * volume logic in sink-input.c needs also a function that doesn't do all the
 * extra stuff that pa_sink_set_volume() does. This function simply sets
 * s->reference_volume and fires change notifications. */
void pa_sink_set_reference_volume_direct(pa_sink *s, const pa_cvolume *volume);

/* When the default_sink is changed or the active_port of a sink is changed to
 * PA_AVAILABLE_NO, this function is called to move the streams of the old
 * default_sink or the sink with active_port equals PA_AVAILABLE_NO to the
 * current default_sink conditionally*/
void pa_sink_move_streams_to_default_sink(pa_core *core, pa_sink *old_sink, bool default_sink_changed);

/* Verify that we called in IO context (aka 'thread context), or that
 * the sink is not yet set up, i.e. the thread not set up yet. See
 * pa_assert_io_context() in thread-mq.h for more information. */
#define pa_sink_assert_io_context(s) \
    pa_assert(pa_thread_mq_get() || !PA_SINK_IS_LINKED((s)->state))

#endif
