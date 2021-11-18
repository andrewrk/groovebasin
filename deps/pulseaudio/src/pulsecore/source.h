#ifndef foopulsesourcehfoo
#define foopulsesourcehfoo

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
#include <pulsecore/sink.h>
#include <pulsecore/module.h>
#include <pulsecore/asyncmsgq.h>
#include <pulsecore/msgobject.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/card.h>
#include <pulsecore/device-port.h>
#include <pulsecore/queue.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/source-output.h>

#define PA_MAX_OUTPUTS_PER_SOURCE 256

/* Returns true if source is linked: registered and accessible from client side. */
static inline bool PA_SOURCE_IS_LINKED(pa_source_state_t x) {
    return x == PA_SOURCE_RUNNING || x == PA_SOURCE_IDLE || x == PA_SOURCE_SUSPENDED;
}

/* A generic definition for void callback functions */
typedef void(*pa_source_cb_t)(pa_source *s);

typedef int (*pa_source_get_mute_cb_t)(pa_source *s, bool *mute);

struct pa_source {
    pa_msgobject parent;

    uint32_t index;
    pa_core *core;

    pa_source_state_t state;

    /* Set in the beginning of pa_source_unlink() before setting the source
     * state to UNLINKED. The purpose is to prevent moving streams to a source
     * that is about to be removed. */
    bool unlink_requested;

    pa_source_flags_t flags;
    pa_suspend_cause_t suspend_cause;

    char *name;
    char *driver;                             /* may be NULL */
    pa_proplist *proplist;

    pa_module *module;                        /* may be NULL */
    pa_card *card;                            /* may be NULL */

    pa_sample_spec sample_spec;
    pa_channel_map channel_map;
    uint32_t default_sample_rate;
    uint32_t alternate_sample_rate;
    bool avoid_resampling:1;

    pa_idxset *outputs;
    unsigned n_corked;
    pa_sink *monitor_of;                     /* may be NULL */
    pa_source_output *output_from_master;    /* non-NULL only for filter sources */

    pa_volume_t base_volume; /* shall be constant */
    unsigned n_volume_steps; /* shall be constant */

    /* Also see http://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/Developer/Volumes/ */
    pa_cvolume reference_volume; /* The volume exported and taken as reference base for relative source output volumes */
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

    /* Callbacks for doing things when the source state and/or suspend cause is
     * changed. It's fine to set either or both of the callbacks to NULL if the
     * implementation doesn't have anything to do on state or suspend cause
     * changes.
     *
     * set_state_in_main_thread() is called first. The callback is allowed to
     * report failure if and only if the source changes its state from
     * SUSPENDED to IDLE or RUNNING. (FIXME: It would make sense to allow
     * failure also when changing state from INIT to IDLE or RUNNING, but
     * currently that will crash pa_source_put().) If
     * set_state_in_main_thread() fails, set_state_in_io_thread() won't be
     * called.
     *
     * If set_state_in_main_thread() is successful (or not set), then
     * set_state_in_io_thread() is called. Again, failure is allowed if and
     * only if the source changes state from SUSPENDED to IDLE or RUNNING. If
     * set_state_in_io_thread() fails, then set_state_in_main_thread() is
     * called again, this time with the state parameter set to SUSPENDED and
     * the suspend_cause parameter set to 0.
     *
     * pa_source.state, pa_source.thread_info.state and pa_source.suspend_cause
     * are updated only after all the callback calls. In case of failure, the
     * state is set to SUSPENDED and the suspend cause is set to 0. */
    int (*set_state_in_main_thread)(pa_source *s, pa_source_state_t state, pa_suspend_cause_t suspend_cause); /* may be NULL */
    int (*set_state_in_io_thread)(pa_source *s, pa_source_state_t state, pa_suspend_cause_t suspend_cause); /* may be NULL */

    /* Called when the volume is queried. Called from main loop
     * context. If this is NULL a PA_SOURCE_MESSAGE_GET_VOLUME message
     * will be sent to the IO thread instead. If refresh_volume is
     * false neither this function is called nor a message is sent.
     *
     * You must use the function pa_source_set_get_volume_callback() to
     * set this callback. */
    pa_source_cb_t get_volume; /* may be NULL */

    /* Called when the volume shall be changed. Called from main loop
     * context. If this is NULL a PA_SOURCE_MESSAGE_SET_VOLUME message
     * will be sent to the IO thread instead.
     *
     * You must use the function pa_source_set_set_volume_callback() to
     * set this callback. */
    pa_source_cb_t set_volume; /* may be NULL */

    /* Source drivers that set PA_SOURCE_DEFERRED_VOLUME must provide this
     * callback. This callback is not used with source that do not set
     * PA_SOURCE_DEFERRED_VOLUME. This is called from the IO thread when a
     * pending hardware volume change has to be written to the
     * hardware. The requested volume is passed to the callback
     * implementation in s->thread_info.current_hw_volume.
     *
     * The call is done inside pa_source_volume_change_apply(), which is
     * not called automatically - it is the driver's responsibility to
     * schedule that function to be called at the right times in the
     * IO thread.
     *
     * You must use the function pa_source_set_write_volume_callback() to
     * set this callback. */
    pa_source_cb_t write_volume; /* may be NULL */

    /* If the source mute can change "spontaneously" (i.e. initiated by the
     * source implementation, not by someone else calling
     * pa_source_set_mute()), then the source implementation can notify about
     * changed mute either by calling pa_source_mute_changed() or by calling
     * pa_source_get_mute() with force_refresh=true. If the implementation
     * chooses the latter approach, it should implement the get_mute callback.
     * Otherwise get_mute can be NULL.
     *
     * This is called when pa_source_get_mute() is called with
     * force_refresh=true. This is called from the IO thread if the
     * PA_SINK_DEFERRED_VOLUME flag is set, otherwise this is called from the
     * main thread. On success, the implementation is expected to return 0 and
     * set the mute parameter that is passed as a reference. On failure, the
     * implementation is expected to return -1.
     *
     * You must use the function pa_source_set_get_mute_callback() to
     * set this callback. */
    pa_source_get_mute_cb_t get_mute;

    /* Called when the mute setting shall be changed. Called from main
     * loop context. If this is NULL a PA_SOURCE_MESSAGE_SET_MUTE
     * message will be sent to the IO thread instead.
     *
     * You must use the function pa_source_set_set_mute_callback() to
     * set this callback. */
    pa_source_cb_t set_mute; /* may be NULL */

    /* Called when a the requested latency is changed. Called from IO
     * thread context. */
    pa_source_cb_t update_requested_latency; /* may be NULL */

    /* Called whenever the port shall be changed. Called from the main
     * thread. */
    int (*set_port)(pa_source *s, pa_device_port *port); /*ditto */

    /* Called to get the list of formats supported by the source, sorted
     * in descending order of preference. */
    pa_idxset* (*get_formats)(pa_source *s); /* ditto */

    /* Called whenever device parameters need to be changed. Called from
     * main thread. */
    void (*reconfigure)(pa_source *s, pa_sample_spec *spec, bool passthrough);

    /* Contains copies of the above data so that the real-time worker
     * thread can work without access locking */
    struct {
        pa_source_state_t state;
        pa_hashmap *outputs;

        pa_rtpoll *rtpoll;

        pa_cvolume soft_volume;
        bool soft_muted:1;

        bool requested_latency_valid:1;
        pa_usec_t requested_latency;

        /* Then number of bytes this source will be rewound for at
         * max. (Only used on monitor sources) */
        size_t max_rewind;

        pa_usec_t min_latency; /* we won't go below this latency */
        pa_usec_t max_latency; /* An upper limit for the latencies */

        pa_usec_t fixed_latency; /* for sources with PA_SOURCE_DYNAMIC_LATENCY this is 0 */

        /* This latency offset is a direct copy from s->port_latency_offset */
        int64_t port_latency_offset;

        /* Delayed volume change events are queued here. The events
         * are stored in expiration order. The one expiring next is in
         * the head of the list. */
        PA_LLIST_HEAD(pa_source_volume_change, volume_changes);
        pa_source_volume_change *volume_changes_tail;
        /* This value is updated in pa_source_volume_change_apply() and
         * used only by sources with PA_SOURCE_DEFERRED_VOLUME. */
        pa_cvolume current_hw_volume;

        /* The amount of usec volume up events are delayed and volume
         * down events are made earlier. */
        uint32_t volume_change_safety_margin;
        /* Usec delay added to all volume change events, may be negative. */
        int32_t volume_change_extra_delay;
    } thread_info;

    void *userdata;
};

PA_DECLARE_PUBLIC_CLASS(pa_source);
#define PA_SOURCE(s) pa_source_cast(s)

typedef enum pa_source_message {
    PA_SOURCE_MESSAGE_ADD_OUTPUT,
    PA_SOURCE_MESSAGE_REMOVE_OUTPUT,
    PA_SOURCE_MESSAGE_GET_VOLUME,
    PA_SOURCE_MESSAGE_SET_SHARED_VOLUME,
    PA_SOURCE_MESSAGE_SET_VOLUME_SYNCED,
    PA_SOURCE_MESSAGE_SET_VOLUME,
    PA_SOURCE_MESSAGE_SYNC_VOLUMES,
    PA_SOURCE_MESSAGE_GET_MUTE,
    PA_SOURCE_MESSAGE_SET_MUTE,
    PA_SOURCE_MESSAGE_GET_LATENCY,
    PA_SOURCE_MESSAGE_GET_REQUESTED_LATENCY,
    PA_SOURCE_MESSAGE_SET_STATE,
    PA_SOURCE_MESSAGE_SET_LATENCY_RANGE,
    PA_SOURCE_MESSAGE_GET_LATENCY_RANGE,
    PA_SOURCE_MESSAGE_SET_FIXED_LATENCY,
    PA_SOURCE_MESSAGE_GET_FIXED_LATENCY,
    PA_SOURCE_MESSAGE_GET_MAX_REWIND,
    PA_SOURCE_MESSAGE_SET_MAX_REWIND,
    PA_SOURCE_MESSAGE_UPDATE_VOLUME_AND_MUTE,
    PA_SOURCE_MESSAGE_SET_PORT_LATENCY_OFFSET,
    PA_SOURCE_MESSAGE_MAX
} pa_source_message_t;

typedef struct pa_source_new_data {
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

    bool volume_is_set:1;
    bool muted_is_set:1;
    bool sample_spec_is_set:1;
    bool channel_map_is_set:1;
    bool alternate_sample_rate_is_set:1;
    bool avoid_resampling_is_set:1;

    bool namereg_fail:1;

    bool save_port:1;
    bool save_volume:1;
    bool save_muted:1;
} pa_source_new_data;

pa_source_new_data* pa_source_new_data_init(pa_source_new_data *data);
void pa_source_new_data_set_name(pa_source_new_data *data, const char *name);
void pa_source_new_data_set_sample_spec(pa_source_new_data *data, const pa_sample_spec *spec);
void pa_source_new_data_set_channel_map(pa_source_new_data *data, const pa_channel_map *map);
void pa_source_new_data_set_alternate_sample_rate(pa_source_new_data *data, const uint32_t alternate_sample_rate);
void pa_source_new_data_set_avoid_resampling(pa_source_new_data *data, bool avoid_resampling);
void pa_source_new_data_set_volume(pa_source_new_data *data, const pa_cvolume *volume);
void pa_source_new_data_set_muted(pa_source_new_data *data, bool mute);
void pa_source_new_data_set_port(pa_source_new_data *data, const char *port);
void pa_source_new_data_done(pa_source_new_data *data);

/*** To be called exclusively by the source driver, from main context */

pa_source* pa_source_new(
        pa_core *core,
        pa_source_new_data *data,
        pa_source_flags_t flags);

void pa_source_set_get_volume_callback(pa_source *s, pa_source_cb_t cb);
void pa_source_set_set_volume_callback(pa_source *s, pa_source_cb_t cb);
void pa_source_set_write_volume_callback(pa_source *s, pa_source_cb_t cb);
void pa_source_set_get_mute_callback(pa_source *s, pa_source_get_mute_cb_t cb);
void pa_source_set_set_mute_callback(pa_source *s, pa_source_cb_t cb);
void pa_source_enable_decibel_volume(pa_source *s, bool enable);

void pa_source_put(pa_source *s);
void pa_source_unlink(pa_source *s);

void pa_source_set_description(pa_source *s, const char *description);
void pa_source_set_asyncmsgq(pa_source *s, pa_asyncmsgq *q);
void pa_source_set_rtpoll(pa_source *s, pa_rtpoll *p);

void pa_source_set_max_rewind(pa_source *s, size_t max_rewind);
void pa_source_set_latency_range(pa_source *s, pa_usec_t min_latency, pa_usec_t max_latency);
void pa_source_set_fixed_latency(pa_source *s, pa_usec_t latency);

void pa_source_set_soft_volume(pa_source *s, const pa_cvolume *volume);
void pa_source_volume_changed(pa_source *s, const pa_cvolume *new_volume);
void pa_source_mute_changed(pa_source *s, bool new_muted);

int pa_source_sync_suspend(pa_source *s);

void pa_source_update_flags(pa_source *s, pa_source_flags_t mask, pa_source_flags_t value);

/*** May be called by everyone, from main context */

void pa_source_set_port_latency_offset(pa_source *s, int64_t offset);

/* The returned value is supposed to be in the time domain of the sound card! */
pa_usec_t pa_source_get_latency(pa_source *s);
pa_usec_t pa_source_get_requested_latency(pa_source *s);
void pa_source_get_latency_range(pa_source *s, pa_usec_t *min_latency, pa_usec_t *max_latency);
pa_usec_t pa_source_get_fixed_latency(pa_source *s);

size_t pa_source_get_max_rewind(pa_source *s);

int pa_source_update_status(pa_source*s);
int pa_source_suspend(pa_source *s, bool suspend, pa_suspend_cause_t cause);
int pa_source_suspend_all(pa_core *c, bool suspend, pa_suspend_cause_t cause);

/* Use this instead of checking s->flags & PA_SOURCE_FLAT_VOLUME directly. */
bool pa_source_flat_volume_enabled(pa_source *s);

/* Get the master source when sharing volumes */
pa_source *pa_source_get_master(pa_source *s);

bool pa_source_is_filter(pa_source *s);

/* Is the source in passthrough mode? (that is, is this a monitor source for a sink
 * that has a passthrough sink input connected to it. */
bool pa_source_is_passthrough(pa_source *s);
/* These should be called when a source enters/leaves passthrough mode */
void pa_source_enter_passthrough(pa_source *s);
void pa_source_leave_passthrough(pa_source *s);

void pa_source_set_volume(pa_source *source, const pa_cvolume *volume, bool sendmsg, bool save);
const pa_cvolume *pa_source_get_volume(pa_source *source, bool force_refresh);

void pa_source_set_mute(pa_source *source, bool mute, bool save);
bool pa_source_get_mute(pa_source *source, bool force_refresh);

bool pa_source_update_proplist(pa_source *s, pa_update_mode_t mode, pa_proplist *p);

int pa_source_set_port(pa_source *s, const char *name, bool save);

void pa_source_reconfigure(pa_source *s, pa_sample_spec *spec, bool passthrough);

unsigned pa_source_linked_by(pa_source *s); /* Number of connected streams */
unsigned pa_source_used_by(pa_source *s); /* Number of connected streams that are not corked */

/* Returns how many streams are active that don't allow suspensions. If
 * "ignore" is non-NULL, that stream is not included in the count. */
unsigned pa_source_check_suspend(pa_source *s, pa_source_output *ignore);

const char *pa_source_state_to_string(pa_source_state_t state);

/* Moves all inputs away, and stores them in pa_queue */
pa_queue *pa_source_move_all_start(pa_source *s, pa_queue *q);
void pa_source_move_all_finish(pa_source *s, pa_queue *q, bool save);
void pa_source_move_all_fail(pa_queue *q);

/* Returns a copy of the source formats. TODO: Get rid of this function (or at
 * least get rid of the copying). There's no good reason to copy the formats
 * every time someone wants to know what formats the source supports. The
 * formats idxset could be stored directly in the pa_source struct.
 * https://bugs.freedesktop.org/show_bug.cgi?id=71924 */
pa_idxset* pa_source_get_formats(pa_source *s);

bool pa_source_check_format(pa_source *s, pa_format_info *f);
pa_idxset* pa_source_check_formats(pa_source *s, pa_idxset *in_formats);

void pa_source_set_sample_format(pa_source *s, pa_sample_format_t format);
void pa_source_set_sample_rate(pa_source *s, uint32_t rate);

/*** To be called exclusively by the source driver, from IO context */

void pa_source_post(pa_source*s, const pa_memchunk *chunk);
void pa_source_post_direct(pa_source*s, pa_source_output *o, const pa_memchunk *chunk);
void pa_source_process_rewind(pa_source *s, size_t nbytes);

int pa_source_process_msg(pa_msgobject *o, int code, void *userdata, int64_t, pa_memchunk *chunk);

void pa_source_attach_within_thread(pa_source *s);
void pa_source_detach_within_thread(pa_source *s);

pa_usec_t pa_source_get_requested_latency_within_thread(pa_source *s);

void pa_source_set_max_rewind_within_thread(pa_source *s, size_t max_rewind);

void pa_source_set_latency_range_within_thread(pa_source *s, pa_usec_t min_latency, pa_usec_t max_latency);
void pa_source_set_fixed_latency_within_thread(pa_source *s, pa_usec_t latency);

void pa_source_update_volume_and_mute(pa_source *s);

bool pa_source_volume_change_apply(pa_source *s, pa_usec_t *usec_to_next);

/*** To be called exclusively by source output drivers, from IO context */

void pa_source_invalidate_requested_latency(pa_source *s, bool dynamic);
int64_t pa_source_get_latency_within_thread(pa_source *s, bool allow_negative);

/* Called from the main thread, from source-output.c only. The normal way to
 * set the source reference volume is to call pa_source_set_volume(), but the
 * flat volume logic in source-output.c needs also a function that doesn't do
 * all the extra stuff that pa_source_set_volume() does. This function simply
 * sets s->reference_volume and fires change notifications. */
void pa_source_set_reference_volume_direct(pa_source *s, const pa_cvolume *volume);

/* When the default_source is changed or the active_port of a source is changed to
 * PA_AVAILABLE_NO, this function is called to move the streams of the old
 * default_source or the source with active_port equals PA_AVAILABLE_NO to the
 * current default_source conditionally*/
void pa_source_move_streams_to_default_source(pa_core *core, pa_source *old_source, bool default_source_changed);

#define pa_source_assert_io_context(s) \
    pa_assert(pa_thread_mq_get() || !PA_SOURCE_IS_LINKED((s)->state))

#endif
