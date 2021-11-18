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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/version.h>
#include <pulse/utf8.h>
#include <pulse/util.h>
#include <pulse/xmalloc.h>
#include <pulse/internal.h>

#include <pulsecore/native-common.h>
#include <pulsecore/packet.h>
#include <pulsecore/client.h>
#include <pulsecore/source-output.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/pstream.h>
#include <pulsecore/tagstruct.h>
#include <pulsecore/pdispatch.h>
#include <pulsecore/pstream-util.h>
#include <pulsecore/namereg.h>
#include <pulsecore/core-scache.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/log.h>
#include <pulsecore/mem.h>
#include <pulsecore/strlist.h>
#include <pulsecore/shared.h>
#include <pulsecore/sample-util.h>
#include <pulsecore/creds.h>
#include <pulsecore/core-util.h>
#include <pulsecore/ipacl.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/mem.h>

#include "protocol-native.h"

/* #define PROTOCOL_NATIVE_DEBUG */

/* Kick a client if it doesn't authenticate within this time */
#define AUTH_TIMEOUT (60 * PA_USEC_PER_SEC)

/* Don't accept more connection than this */
#define MAX_CONNECTIONS 64

#define MAX_MEMBLOCKQ_LENGTH (4*1024*1024) /* 4MB */
#define DEFAULT_TLENGTH_MSEC 2000 /* 2s */
#define DEFAULT_PROCESS_MSEC 20   /* 20ms */
#define DEFAULT_FRAGSIZE_MSEC DEFAULT_TLENGTH_MSEC

struct pa_native_protocol;

typedef struct record_stream {
    pa_msgobject parent;

    pa_native_connection *connection;
    uint32_t index;

    pa_source_output *source_output;
    pa_memblockq *memblockq;

    bool adjust_latency:1;
    bool early_requests:1;

    /* Requested buffer attributes */
    pa_buffer_attr buffer_attr_req;
    /* Fixed-up and adjusted buffer attributes */
    pa_buffer_attr buffer_attr;

    pa_atomic_t on_the_fly;
    pa_usec_t configured_source_latency;
    size_t drop_initial;

    /* Only updated after SOURCE_OUTPUT_MESSAGE_UPDATE_LATENCY */
    size_t on_the_fly_snapshot;
    pa_usec_t current_monitor_latency;
    pa_usec_t current_source_latency;
} record_stream;

#define RECORD_STREAM(o) (record_stream_cast(o))
PA_DEFINE_PRIVATE_CLASS(record_stream, pa_msgobject);

typedef struct output_stream {
    pa_msgobject parent;
} output_stream;

#define OUTPUT_STREAM(o) (output_stream_cast(o))
PA_DEFINE_PRIVATE_CLASS(output_stream, pa_msgobject);

typedef struct playback_stream {
    output_stream parent;

    pa_native_connection *connection;
    uint32_t index;

    pa_sink_input *sink_input;
    pa_memblockq *memblockq;

    bool adjust_latency:1;
    bool early_requests:1;

    bool is_underrun:1;
    bool drain_request:1;
    uint32_t drain_tag;
    uint32_t syncid;

    /* Optimization to avoid too many rewinds with a lot of small blocks */
    pa_atomic_t seek_or_post_in_queue;
    int64_t seek_windex;

    pa_atomic_t missing;
    pa_usec_t configured_sink_latency;
    /* Requested buffer attributes */
    pa_buffer_attr buffer_attr_req;
    /* Fixed-up and adjusted buffer attributes */
    pa_buffer_attr buffer_attr;

    /* Only updated after SINK_INPUT_MESSAGE_UPDATE_LATENCY */
    int64_t read_index, write_index;
    size_t render_memblockq_length;
    pa_usec_t current_sink_latency;
    uint64_t playing_for, underrun_for;
} playback_stream;

#define PLAYBACK_STREAM(o) (playback_stream_cast(o))
PA_DEFINE_PRIVATE_CLASS(playback_stream, output_stream);

typedef struct upload_stream {
    output_stream parent;

    pa_native_connection *connection;
    uint32_t index;

    pa_memchunk memchunk;
    size_t length;
    char *name;
    pa_sample_spec sample_spec;
    pa_channel_map channel_map;
    pa_proplist *proplist;
} upload_stream;

#define UPLOAD_STREAM(o) (upload_stream_cast(o))
PA_DEFINE_PRIVATE_CLASS(upload_stream, output_stream);

struct pa_native_connection {
    pa_msgobject parent;
    pa_native_protocol *protocol;
    pa_native_options *options;
    bool authorized:1;
    bool is_local:1;
    uint32_t version;
    pa_client *client;
    /* R/W mempool, one per client connection, for srbchannel transport.
     * Both server and client can write to this shm area.
     *
     * Note: This will be NULL if our connection with the client does
     * not support srbchannels */
    pa_mempool *rw_mempool;
    pa_pstream *pstream;
    pa_pdispatch *pdispatch;
    pa_idxset *record_streams, *output_streams;
    uint32_t rrobin_index;
    pa_subscription *subscription;
    pa_time_event *auth_timeout_event;
    pa_srbchannel *srbpending;
};

#define PA_NATIVE_CONNECTION(o) (pa_native_connection_cast(o))
PA_DEFINE_PRIVATE_CLASS(pa_native_connection, pa_msgobject);

struct pa_native_protocol {
    PA_REFCNT_DECLARE;

    pa_core *core;
    pa_idxset *connections;

    pa_strlist *servers;
    pa_hook hooks[PA_NATIVE_HOOK_MAX];

    pa_hashmap *extensions;
};

enum {
    SOURCE_OUTPUT_MESSAGE_UPDATE_LATENCY = PA_SOURCE_OUTPUT_MESSAGE_MAX
};

enum {
    SINK_INPUT_MESSAGE_POST_DATA = PA_SINK_INPUT_MESSAGE_MAX, /* data from main loop to sink input */
    SINK_INPUT_MESSAGE_DRAIN, /* disabled prebuf, get playback started. */
    SINK_INPUT_MESSAGE_FLUSH,
    SINK_INPUT_MESSAGE_TRIGGER,
    SINK_INPUT_MESSAGE_SEEK,
    SINK_INPUT_MESSAGE_PREBUF_FORCE,
    SINK_INPUT_MESSAGE_UPDATE_LATENCY,
    SINK_INPUT_MESSAGE_UPDATE_BUFFER_ATTR
};

enum {
    PLAYBACK_STREAM_MESSAGE_REQUEST_DATA,      /* data requested from sink input from the main loop */
    PLAYBACK_STREAM_MESSAGE_UNDERFLOW,
    PLAYBACK_STREAM_MESSAGE_OVERFLOW,
    PLAYBACK_STREAM_MESSAGE_DRAIN_ACK,
    PLAYBACK_STREAM_MESSAGE_STARTED,
    PLAYBACK_STREAM_MESSAGE_UPDATE_TLENGTH
};

enum {
    RECORD_STREAM_MESSAGE_POST_DATA         /* data from source output to main loop */
};

enum {
    CONNECTION_MESSAGE_RELEASE,
    CONNECTION_MESSAGE_REVOKE
};

static bool sink_input_process_underrun_cb(pa_sink_input *i);
static int sink_input_pop_cb(pa_sink_input *i, size_t length, pa_memchunk *chunk);
static void sink_input_kill_cb(pa_sink_input *i);
static void sink_input_suspend_cb(pa_sink_input *i, pa_sink_state_t old_state, pa_suspend_cause_t old_suspend_cause);
static void sink_input_moving_cb(pa_sink_input *i, pa_sink *dest);
static void sink_input_process_rewind_cb(pa_sink_input *i, size_t nbytes);
static void sink_input_update_max_rewind_cb(pa_sink_input *i, size_t nbytes);
static void sink_input_update_max_request_cb(pa_sink_input *i, size_t nbytes);
static void sink_input_send_event_cb(pa_sink_input *i, const char *event, pa_proplist *pl);

static void native_connection_send_memblock(pa_native_connection *c);
static void playback_stream_request_bytes(struct playback_stream*s);

static void source_output_kill_cb(pa_source_output *o);
static void source_output_push_cb(pa_source_output *o, const pa_memchunk *chunk);
static void source_output_suspend_cb(pa_source_output *o, pa_source_state_t old_state, pa_suspend_cause_t old_suspend_cause);
static void source_output_moving_cb(pa_source_output *o, pa_source *dest);
static pa_usec_t source_output_get_latency_cb(pa_source_output *o);
static void source_output_send_event_cb(pa_source_output *o, const char *event, pa_proplist *pl);

static int sink_input_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk);
static int source_output_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk);

/* structure management */

/* Called from main context */
static void upload_stream_unlink(upload_stream *s) {
    pa_assert(s);

    if (!s->connection)
        return;

    pa_assert_se(pa_idxset_remove_by_data(s->connection->output_streams, s, NULL) == s);
    s->connection = NULL;
    upload_stream_unref(s);
}

/* Called from main context */
static void upload_stream_free(pa_object *o) {
    upload_stream *s = UPLOAD_STREAM(o);
    pa_assert(s);

    upload_stream_unlink(s);

    pa_xfree(s->name);

    if (s->proplist)
        pa_proplist_free(s->proplist);

    if (s->memchunk.memblock)
        pa_memblock_unref(s->memchunk.memblock);

    pa_xfree(s);
}

/* Called from main context */
static upload_stream* upload_stream_new(
        pa_native_connection *c,
        const pa_sample_spec *ss,
        const pa_channel_map *map,
        const char *name,
        size_t length,
        pa_proplist *p) {

    upload_stream *s;

    pa_assert(c);
    pa_assert(ss);
    pa_assert(name);
    pa_assert(length > 0);
    pa_assert(p);

    s = pa_msgobject_new(upload_stream);
    s->parent.parent.parent.free = upload_stream_free;
    s->connection = c;
    s->sample_spec = *ss;
    s->channel_map = *map;
    s->name = pa_xstrdup(name);
    pa_memchunk_reset(&s->memchunk);
    s->length = length;
    s->proplist = pa_proplist_copy(p);
    pa_proplist_update(s->proplist, PA_UPDATE_MERGE, c->client->proplist);

    pa_idxset_put(c->output_streams, s, &s->index);

    return s;
}

/* Called from main context */
static void record_stream_unlink(record_stream *s) {
    pa_assert(s);

    if (!s->connection)
        return;

    if (s->source_output) {
        pa_source_output_unlink(s->source_output);
        pa_source_output_unref(s->source_output);
        s->source_output = NULL;
    }

    pa_assert_se(pa_idxset_remove_by_data(s->connection->record_streams, s, NULL) == s);
    s->connection = NULL;
    record_stream_unref(s);
}

/* Called from main context */
static void record_stream_free(pa_object *o) {
    record_stream *s = RECORD_STREAM(o);
    pa_assert(s);

    record_stream_unlink(s);

    pa_memblockq_free(s->memblockq);
    pa_xfree(s);
}

/* Called from main context */
static int record_stream_process_msg(pa_msgobject *o, int code, void*userdata, int64_t offset, pa_memchunk *chunk) {
    record_stream *s = RECORD_STREAM(o);
    record_stream_assert_ref(s);

    if (!s->connection)
        return -1;

    switch (code) {

        case RECORD_STREAM_MESSAGE_POST_DATA:

            /* We try to keep up to date with how many bytes are
             * currently on the fly */
            pa_atomic_sub(&s->on_the_fly, chunk->length);

            if (pa_memblockq_push_align(s->memblockq, chunk) < 0) {
/*                 pa_log_warn("Failed to push data into output queue."); */
                return -1;
            }

            if (!pa_pstream_is_pending(s->connection->pstream))
                native_connection_send_memblock(s->connection);

            break;
    }

    return 0;
}

/* Called from main context */
static void fix_record_buffer_attr_pre(record_stream *s) {

    size_t frame_size;
    pa_usec_t orig_fragsize_usec, fragsize_usec, source_usec;

    pa_assert(s);

    /* This function will be called from the main thread, before as
     * well as after the source output has been activated using
     * pa_source_output_put()! That means it may not touch any
     * ->thread_info data! */

    frame_size = pa_frame_size(&s->source_output->sample_spec);
    s->buffer_attr = s->buffer_attr_req;

    if (s->buffer_attr.maxlength == (uint32_t) -1 || s->buffer_attr.maxlength > MAX_MEMBLOCKQ_LENGTH)
        s->buffer_attr.maxlength = MAX_MEMBLOCKQ_LENGTH;
    if (s->buffer_attr.maxlength <= 0)
        s->buffer_attr.maxlength = (uint32_t) frame_size;

    if (s->buffer_attr.fragsize == (uint32_t) -1)
        s->buffer_attr.fragsize = (uint32_t) pa_usec_to_bytes(DEFAULT_FRAGSIZE_MSEC*PA_USEC_PER_MSEC, &s->source_output->sample_spec);
    if (s->buffer_attr.fragsize <= 0)
        s->buffer_attr.fragsize = (uint32_t) frame_size;

    orig_fragsize_usec = fragsize_usec = pa_bytes_to_usec(s->buffer_attr.fragsize, &s->source_output->sample_spec);

    if (s->early_requests) {

        /* In early request mode we need to emulate the classic
         * fragment-based playback model. Unfortunately we have no
         * mechanism to tell the source how often we want it to send us
         * data. The next best thing we can do is to set the source's
         * total buffer (i.e. its latency) to the fragment size. That
         * way it will have to send data at least that often. */

        source_usec = fragsize_usec;

    } else if (s->adjust_latency) {

        /* So, the user asked us to adjust the latency according to
         * what the source can provide. We set the source to whatever
         * latency it can provide that is closest to what we want, and
         * let the client buffer be equally large. This does NOT mean
         * that we are doing (2 * fragsize) bytes of buffering, since
         * the client-side buffer is only data that is on the way to
         * the client. */

        source_usec = fragsize_usec;

    } else {

        /* Ok, the user didn't ask us to adjust the latency, hence we
         * don't */

        source_usec = (pa_usec_t) -1;
    }

    if (source_usec != (pa_usec_t) -1)
        s->configured_source_latency = pa_source_output_set_requested_latency(s->source_output, source_usec);
    else
        s->configured_source_latency = 0;

    if (s->early_requests) {

        /* Ok, we didn't necessarily get what we were asking for. We
         * might still get the proper fragment interval, we just can't
         * guarantee it. */

        if (fragsize_usec != s->configured_source_latency)
            pa_log_debug("Could not configure a sufficiently low latency. Early requests might not be satisfied.");

    } else if (s->adjust_latency) {

        /* We keep the client buffer large enough to transfer one
         * hardware-buffer-sized chunk at a time to the client. */

        fragsize_usec = s->configured_source_latency;
    }

    if (pa_usec_to_bytes(orig_fragsize_usec, &s->source_output->sample_spec) !=
        pa_usec_to_bytes(fragsize_usec, &s->source_output->sample_spec))

        s->buffer_attr.fragsize = (uint32_t) pa_usec_to_bytes(fragsize_usec, &s->source_output->sample_spec);

    if (s->buffer_attr.fragsize <= 0)
        s->buffer_attr.fragsize = (uint32_t) frame_size;
}

/* Called from main context */
static void fix_record_buffer_attr_post(record_stream *s) {
    size_t base;

    pa_assert(s);

    /* This function will be called from the main thread, before as
     * well as after the source output has been activated using
     * pa_source_output_put()! That means it may not touch and
     * ->thread_info data! */

    base = pa_frame_size(&s->source_output->sample_spec);

    s->buffer_attr.fragsize = (s->buffer_attr.fragsize/base)*base;
    if (s->buffer_attr.fragsize <= 0)
        s->buffer_attr.fragsize = base;

    if (s->buffer_attr.fragsize > s->buffer_attr.maxlength)
        s->buffer_attr.fragsize = s->buffer_attr.maxlength;
}

/* Called from main context */
static record_stream* record_stream_new(
        pa_native_connection *c,
        pa_source *source,
        pa_sample_spec *ss,
        pa_channel_map *map,
        pa_idxset *formats,
        pa_buffer_attr *attr,
        pa_cvolume *volume,
        bool muted,
        bool muted_set,
        pa_source_output_flags_t flags,
        pa_proplist *p,
        bool adjust_latency,
        bool early_requests,
        bool relative_volume,
        bool peak_detect,
        pa_sink_input *direct_on_input,
        int *ret) {

    /* Note: This function takes ownership of the 'formats' param, so we need
     * to take extra care to not leak it */

    record_stream *s;
    pa_source_output *source_output = NULL;
    pa_source_output_new_data data;
    char *memblockq_name;

    pa_assert(c);
    pa_assert(ss);
    pa_assert(p);
    pa_assert(ret);

    pa_source_output_new_data_init(&data);

    pa_proplist_update(data.proplist, PA_UPDATE_REPLACE, p);
    data.driver = __FILE__;
    data.module = c->options->module;
    data.client = c->client;
    if (source)
        pa_source_output_new_data_set_source(&data, source, false, true);
    if (pa_sample_spec_valid(ss))
        pa_source_output_new_data_set_sample_spec(&data, ss);
    if (pa_channel_map_valid(map))
        pa_source_output_new_data_set_channel_map(&data, map);
    if (formats)
        pa_source_output_new_data_set_formats(&data, formats);
    data.direct_on_input = direct_on_input;
    if (volume) {
        pa_source_output_new_data_set_volume(&data, volume);
        data.volume_is_absolute = !relative_volume;
        data.save_volume = false;
    }
    if (muted_set) {
        pa_source_output_new_data_set_muted(&data, muted);
        data.save_muted = false;
    }
    if (peak_detect)
        data.resample_method = PA_RESAMPLER_PEAKS;
    data.flags = flags;

    *ret = -pa_source_output_new(&source_output, c->protocol->core, &data);

    pa_source_output_new_data_done(&data);

    if (!source_output)
        return NULL;

    s = pa_msgobject_new(record_stream);
    s->parent.parent.free = record_stream_free;
    s->parent.process_msg = record_stream_process_msg;
    s->connection = c;
    s->source_output = source_output;
    s->buffer_attr_req = *attr;
    s->adjust_latency = adjust_latency;
    s->early_requests = early_requests;
    pa_atomic_store(&s->on_the_fly, 0);

    s->source_output->parent.process_msg = source_output_process_msg;
    s->source_output->push = source_output_push_cb;
    s->source_output->kill = source_output_kill_cb;
    s->source_output->get_latency = source_output_get_latency_cb;
    s->source_output->moving = source_output_moving_cb;
    s->source_output->suspend = source_output_suspend_cb;
    s->source_output->send_event = source_output_send_event_cb;
    s->source_output->userdata = s;

    fix_record_buffer_attr_pre(s);

    memblockq_name = pa_sprintf_malloc("native protocol record stream memblockq [%u]", s->source_output->index);
    s->memblockq = pa_memblockq_new(
            memblockq_name,
            0,
            s->buffer_attr.maxlength,
            0,
            &source_output->sample_spec,
            1,
            0,
            0,
            NULL);
    pa_xfree(memblockq_name);

    pa_memblockq_get_attr(s->memblockq, &s->buffer_attr);
    fix_record_buffer_attr_post(s);

    *ss = s->source_output->sample_spec;
    *map = s->source_output->channel_map;

    pa_idxset_put(c->record_streams, s, &s->index);

    pa_log_info("Final latency %0.2f ms = %0.2f ms + %0.2f ms",
                ((double) pa_bytes_to_usec(s->buffer_attr.fragsize, &source_output->sample_spec) + (double) s->configured_source_latency) / PA_USEC_PER_MSEC,
                (double) pa_bytes_to_usec(s->buffer_attr.fragsize, &source_output->sample_spec) / PA_USEC_PER_MSEC,
                (double) s->configured_source_latency / PA_USEC_PER_MSEC);

    pa_source_output_put(s->source_output);
    return s;
}

/* Called from main context */
static void record_stream_send_killed(record_stream *r) {
    pa_tagstruct *t;
    record_stream_assert_ref(r);

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_RECORD_STREAM_KILLED);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_putu32(t, r->index);
    pa_pstream_send_tagstruct(r->connection->pstream, t);
}

/* Called from main context */
static void playback_stream_unlink(playback_stream *s) {
    pa_assert(s);

    if (!s->connection)
        return;

    if (s->sink_input) {
        pa_sink_input_unlink(s->sink_input);
        pa_sink_input_unref(s->sink_input);
        s->sink_input = NULL;
    }

    if (s->drain_request)
        pa_pstream_send_error(s->connection->pstream, s->drain_tag, PA_ERR_NOENTITY);

    pa_assert_se(pa_idxset_remove_by_data(s->connection->output_streams, s, NULL) == s);
    s->connection = NULL;
    playback_stream_unref(s);
}

/* Called from main context */
static void playback_stream_free(pa_object* o) {
    playback_stream *s = PLAYBACK_STREAM(o);
    pa_assert(s);

    playback_stream_unlink(s);

    pa_memblockq_free(s->memblockq);
    pa_xfree(s);
}

/* Called from main context */
static int playback_stream_process_msg(pa_msgobject *o, int code, void*userdata, int64_t offset, pa_memchunk *chunk) {
    playback_stream *s = PLAYBACK_STREAM(o);
    playback_stream_assert_ref(s);

    if (!s->connection)
        return -1;

    switch (code) {

        case PLAYBACK_STREAM_MESSAGE_REQUEST_DATA: {
            pa_tagstruct *t;
            int l = 0;

            for (;;) {
                if ((l = pa_atomic_load(&s->missing)) <= 0)
                    return 0;

                if (pa_atomic_cmpxchg(&s->missing, l, 0))
                    break;
            }

            t = pa_tagstruct_new();
            pa_tagstruct_putu32(t, PA_COMMAND_REQUEST);
            pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
            pa_tagstruct_putu32(t, s->index);
            pa_tagstruct_putu32(t, (uint32_t) l);
            pa_pstream_send_tagstruct(s->connection->pstream, t);

#ifdef PROTOCOL_NATIVE_DEBUG
            pa_log("Requesting %lu bytes", (unsigned long) l);
#endif
            break;
        }

        case PLAYBACK_STREAM_MESSAGE_UNDERFLOW: {
            pa_tagstruct *t;

#ifdef PROTOCOL_NATIVE_DEBUG
            pa_log("signalling underflow");
#endif

            /* Report that we're empty */
            t = pa_tagstruct_new();
            pa_tagstruct_putu32(t, PA_COMMAND_UNDERFLOW);
            pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
            pa_tagstruct_putu32(t, s->index);
            if (s->connection->version >= 23)
                pa_tagstruct_puts64(t, offset);
            pa_pstream_send_tagstruct(s->connection->pstream, t);
            break;
        }

        case PLAYBACK_STREAM_MESSAGE_OVERFLOW: {
            pa_tagstruct *t;

            /* Notify the user we're overflowed*/
            t = pa_tagstruct_new();
            pa_tagstruct_putu32(t, PA_COMMAND_OVERFLOW);
            pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
            pa_tagstruct_putu32(t, s->index);
            pa_pstream_send_tagstruct(s->connection->pstream, t);
            break;
        }

        case PLAYBACK_STREAM_MESSAGE_STARTED:

            if (s->connection->version >= 13) {
                pa_tagstruct *t;

                /* Notify the user we started playback */
                t = pa_tagstruct_new();
                pa_tagstruct_putu32(t, PA_COMMAND_STARTED);
                pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
                pa_tagstruct_putu32(t, s->index);
                pa_pstream_send_tagstruct(s->connection->pstream, t);
            }

            break;

        case PLAYBACK_STREAM_MESSAGE_DRAIN_ACK:
            pa_pstream_send_simple_ack(s->connection->pstream, PA_PTR_TO_UINT(userdata));
            break;

        case PLAYBACK_STREAM_MESSAGE_UPDATE_TLENGTH:

            s->buffer_attr.tlength = (uint32_t) offset;

            if (s->connection->version >= 15) {
                pa_tagstruct *t;

                t = pa_tagstruct_new();
                pa_tagstruct_putu32(t, PA_COMMAND_PLAYBACK_BUFFER_ATTR_CHANGED);
                pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
                pa_tagstruct_putu32(t, s->index);
                pa_tagstruct_putu32(t, s->buffer_attr.maxlength);
                pa_tagstruct_putu32(t, s->buffer_attr.tlength);
                pa_tagstruct_putu32(t, s->buffer_attr.prebuf);
                pa_tagstruct_putu32(t, s->buffer_attr.minreq);
                pa_tagstruct_put_usec(t, s->configured_sink_latency);
                pa_pstream_send_tagstruct(s->connection->pstream, t);
            }

            break;
    }

    return 0;
}

/* Called from main context */
static void fix_playback_buffer_attr(playback_stream *s) {
    size_t frame_size, max_prebuf;
    pa_usec_t orig_tlength_usec, tlength_usec, orig_minreq_usec, minreq_usec, sink_usec;

    pa_assert(s);

#ifdef PROTOCOL_NATIVE_DEBUG
    pa_log("Client requested: maxlength=%li bytes tlength=%li bytes minreq=%li bytes prebuf=%li bytes",
           (long) s->buffer_attr_req.maxlength,
           (long) s->buffer_attr_req.tlength,
           (long) s->buffer_attr_req.minreq,
           (long) s->buffer_attr_req.prebuf);

    pa_log("Client requested: maxlength=%lu ms tlength=%lu ms minreq=%lu ms prebuf=%lu ms",
           (unsigned long) (pa_bytes_to_usec(s->buffer_attr_req.maxlength, &s->sink_input->sample_spec) / PA_USEC_PER_MSEC),
           (unsigned long) (pa_bytes_to_usec(s->buffer_attr_req.tlength, &s->sink_input->sample_spec) / PA_USEC_PER_MSEC),
           (unsigned long) (pa_bytes_to_usec(s->buffer_attr_req.minreq, &s->sink_input->sample_spec) / PA_USEC_PER_MSEC),
           (unsigned long) (pa_bytes_to_usec(s->buffer_attr_req.prebuf, &s->sink_input->sample_spec) / PA_USEC_PER_MSEC));
#endif

    /* This function will be called from the main thread, before as
     * well as after the sink input has been activated using
     * pa_sink_input_put()! That means it may not touch any
     * ->thread_info data, such as the memblockq! */

    frame_size = pa_frame_size(&s->sink_input->sample_spec);
    s->buffer_attr = s->buffer_attr_req;

    if (s->buffer_attr.maxlength == (uint32_t) -1 || s->buffer_attr.maxlength > MAX_MEMBLOCKQ_LENGTH)
        s->buffer_attr.maxlength = MAX_MEMBLOCKQ_LENGTH;
    if (s->buffer_attr.maxlength <= 0)
        s->buffer_attr.maxlength = (uint32_t) frame_size;

    if (s->buffer_attr.tlength == (uint32_t) -1)
        s->buffer_attr.tlength = (uint32_t) pa_usec_to_bytes_round_up(DEFAULT_TLENGTH_MSEC*PA_USEC_PER_MSEC, &s->sink_input->sample_spec);
    if (s->buffer_attr.tlength <= 0)
        s->buffer_attr.tlength = (uint32_t) frame_size;
    if (s->buffer_attr.tlength > s->buffer_attr.maxlength)
        s->buffer_attr.tlength = s->buffer_attr.maxlength;

    if (s->buffer_attr.minreq == (uint32_t) -1) {
        uint32_t process = (uint32_t) pa_usec_to_bytes_round_up(DEFAULT_PROCESS_MSEC*PA_USEC_PER_MSEC, &s->sink_input->sample_spec);
        /* With low-latency, tlength/4 gives a decent default in all of traditional, adjust latency and early request modes. */
        uint32_t m = s->buffer_attr.tlength / 4;
        if (frame_size)
            m -= m % frame_size;
        s->buffer_attr.minreq = PA_MIN(process, m);
    }
    if (s->buffer_attr.minreq <= 0)
        s->buffer_attr.minreq = (uint32_t) frame_size;

    if (s->buffer_attr.tlength < s->buffer_attr.minreq+frame_size)
        s->buffer_attr.tlength = s->buffer_attr.minreq+(uint32_t) frame_size;

    orig_tlength_usec = tlength_usec = pa_bytes_to_usec(s->buffer_attr.tlength, &s->sink_input->sample_spec);
    orig_minreq_usec = minreq_usec = pa_bytes_to_usec(s->buffer_attr.minreq, &s->sink_input->sample_spec);

    pa_log_info("Requested tlength=%0.2f ms, minreq=%0.2f ms",
                (double) tlength_usec / PA_USEC_PER_MSEC,
                (double) minreq_usec / PA_USEC_PER_MSEC);

    if (s->early_requests) {

        /* In early request mode we need to emulate the classic
         * fragment-based playback model. Unfortunately we have no
         * mechanism to tell the sink how often we want to be queried
         * for data. The next best thing we can do is to set the sink's
         * total buffer (i.e. its latency) to the fragment size. That
         * way it will have to query us at least that often. */

        sink_usec = minreq_usec;
        pa_log_debug("Early requests mode enabled, configuring sink latency to minreq.");

    } else if (s->adjust_latency) {

        /* So, the user asked us to adjust the latency of the stream
         * buffer according to the what the sink can provide. The
         * tlength passed in shall be the overall latency. Roughly
         * half the latency will be spent on the hw buffer, the other
         * half of it in the async buffer queue we maintain for each
         * client. In between we'll have a safety space of size
         * 2*minreq. Why the 2*minreq? When the hw buffer is completely
         * empty and needs to be filled, then our buffer must have
         * enough data to fulfill this request immediately and thus
         * have at least the same tlength as the size of the hw
         * buffer. It additionally needs space for 2 times minreq
         * because if the buffer ran empty and a partial fillup
         * happens immediately on the next iteration we need to be
         * able to fulfill it and give the application also minreq
         * time to fill it up again for the next request Makes 2 times
         * minreq in plus.. */

        if (tlength_usec > minreq_usec*2)
            sink_usec = (tlength_usec - minreq_usec*2)/2;
        else
            sink_usec = 0;

        pa_log_debug("Adjust latency mode enabled, configuring sink latency to half of overall latency.");

    } else {

        /* Ok, the user didn't ask us to adjust the latency, but we
         * still need to make sure that the parameters from the user
         * do make sense. */

        if (tlength_usec > minreq_usec*2)
            sink_usec = (tlength_usec - minreq_usec*2);
        else
            sink_usec = 0;

        pa_log_debug("Traditional mode enabled, modifying sink usec only for compat with minreq.");
    }

    s->configured_sink_latency = pa_sink_input_set_requested_latency(s->sink_input, sink_usec);

    if (s->early_requests) {

        /* Ok, we didn't necessarily get what we were asking for. We
         * might still get the proper fragment interval, we just can't
         * guarantee it. */

        if (minreq_usec != s->configured_sink_latency)
            pa_log_debug("Could not configure a sufficiently low latency. Early requests might not be satisfied.");

    } else if (s->adjust_latency) {

        /* Ok, we didn't necessarily get what we were asking for, so
         * let's subtract from what we asked for for the remaining
         * buffer space */

        if (tlength_usec >= s->configured_sink_latency)
            tlength_usec -= s->configured_sink_latency;
    }

    pa_log_debug("Requested latency=%0.2f ms, Received latency=%0.2f ms",
                 (double) sink_usec / PA_USEC_PER_MSEC,
                 (double) s->configured_sink_latency / PA_USEC_PER_MSEC);

    /* FIXME: This is actually larger than necessary, since not all of
     * the sink latency is actually rewritable. */
    if (tlength_usec < s->configured_sink_latency + 2*minreq_usec)
        tlength_usec = s->configured_sink_latency + 2*minreq_usec;

    if (pa_usec_to_bytes_round_up(orig_tlength_usec, &s->sink_input->sample_spec) !=
        pa_usec_to_bytes_round_up(tlength_usec, &s->sink_input->sample_spec))
        s->buffer_attr.tlength = (uint32_t) pa_usec_to_bytes_round_up(tlength_usec, &s->sink_input->sample_spec);

    if (pa_usec_to_bytes(orig_minreq_usec, &s->sink_input->sample_spec) !=
        pa_usec_to_bytes(minreq_usec, &s->sink_input->sample_spec))
        s->buffer_attr.minreq = (uint32_t) pa_usec_to_bytes(minreq_usec, &s->sink_input->sample_spec);

    if (s->buffer_attr.minreq <= 0) {
        s->buffer_attr.minreq = (uint32_t) frame_size;
        s->buffer_attr.tlength += (uint32_t) frame_size*2;
    }

    if (s->buffer_attr.tlength <= s->buffer_attr.minreq)
        s->buffer_attr.tlength = s->buffer_attr.minreq*2 + (uint32_t) frame_size;

    max_prebuf = s->buffer_attr.tlength + (uint32_t)frame_size - s->buffer_attr.minreq;

    if (s->buffer_attr.prebuf == (uint32_t) -1 ||
        s->buffer_attr.prebuf > max_prebuf)
        s->buffer_attr.prebuf = max_prebuf;

#ifdef PROTOCOL_NATIVE_DEBUG
    pa_log("Client accepted: maxlength=%lu ms tlength=%lu ms minreq=%lu ms prebuf=%lu ms",
           (unsigned long) (pa_bytes_to_usec(s->buffer_attr.maxlength, &s->sink_input->sample_spec) / PA_USEC_PER_MSEC),
           (unsigned long) (pa_bytes_to_usec(s->buffer_attr.tlength, &s->sink_input->sample_spec) / PA_USEC_PER_MSEC),
           (unsigned long) (pa_bytes_to_usec(s->buffer_attr.minreq, &s->sink_input->sample_spec) / PA_USEC_PER_MSEC),
           (unsigned long) (pa_bytes_to_usec(s->buffer_attr.prebuf, &s->sink_input->sample_spec) / PA_USEC_PER_MSEC));
#endif
}

/* Called from main context */
static playback_stream* playback_stream_new(
        pa_native_connection *c,
        pa_sink *sink,
        pa_sample_spec *ss,
        pa_channel_map *map,
        pa_idxset *formats,
        pa_buffer_attr *a,
        pa_cvolume *volume,
        bool muted,
        bool muted_set,
        pa_sink_input_flags_t flags,
        pa_proplist *p,
        bool adjust_latency,
        bool early_requests,
        bool relative_volume,
        uint32_t syncid,
        uint32_t *missing,
        int *ret) {

    /* Note: This function takes ownership of the 'formats' param, so we need
     * to take extra care to not leak it */

    playback_stream *ssync;
    playback_stream *s = NULL;
    pa_sink_input *sink_input = NULL;
    pa_memchunk silence;
    uint32_t idx;
    int64_t start_index;
    pa_sink_input_new_data data;
    char *memblockq_name;

    pa_assert(c);
    pa_assert(ss);
    pa_assert(missing);
    pa_assert(p);
    pa_assert(ret);

    /* Find syncid group */
    PA_IDXSET_FOREACH(ssync, c->output_streams, idx) {

        if (!playback_stream_isinstance(ssync))
            continue;

        if (ssync->syncid == syncid)
            break;
    }

    /* Synced streams must connect to the same sink */
    if (ssync) {

        if (!sink)
            sink = ssync->sink_input->sink;
        else if (sink != ssync->sink_input->sink) {
            *ret = PA_ERR_INVALID;
            goto out;
        }
    }

    pa_sink_input_new_data_init(&data);

    pa_proplist_update(data.proplist, PA_UPDATE_REPLACE, p);
    data.driver = __FILE__;
    data.module = c->options->module;
    data.client = c->client;
    if (sink)
        pa_sink_input_new_data_set_sink(&data, sink, false, true);
    if (pa_sample_spec_valid(ss))
        pa_sink_input_new_data_set_sample_spec(&data, ss);
    if (pa_channel_map_valid(map))
        pa_sink_input_new_data_set_channel_map(&data, map);
    if (formats) {
        pa_sink_input_new_data_set_formats(&data, formats);
        /* Ownership transferred to new_data, so we don't free it ourselves */
        formats = NULL;
    }
    if (volume) {
        pa_sink_input_new_data_set_volume(&data, volume);
        data.volume_is_absolute = !relative_volume;
        data.save_volume = false;
    }
    if (muted_set) {
        pa_sink_input_new_data_set_muted(&data, muted);
        data.save_muted = false;
    }
    data.sync_base = ssync ? ssync->sink_input : NULL;
    data.flags = flags;

    *ret = -pa_sink_input_new(&sink_input, c->protocol->core, &data);

    pa_sink_input_new_data_done(&data);

    if (!sink_input)
        goto out;

    s = pa_msgobject_new(playback_stream);
    s->parent.parent.parent.free = playback_stream_free;
    s->parent.parent.process_msg = playback_stream_process_msg;
    s->connection = c;
    s->syncid = syncid;
    s->sink_input = sink_input;
    s->is_underrun = true;
    s->drain_request = false;
    pa_atomic_store(&s->missing, 0);
    s->buffer_attr_req = *a;
    s->adjust_latency = adjust_latency;
    s->early_requests = early_requests;
    pa_atomic_store(&s->seek_or_post_in_queue, 0);
    s->seek_windex = -1;

    s->sink_input->parent.process_msg = sink_input_process_msg;
    s->sink_input->pop = sink_input_pop_cb;
    s->sink_input->process_underrun = sink_input_process_underrun_cb;
    s->sink_input->process_rewind = sink_input_process_rewind_cb;
    s->sink_input->update_max_rewind = sink_input_update_max_rewind_cb;
    s->sink_input->update_max_request = sink_input_update_max_request_cb;
    s->sink_input->kill = sink_input_kill_cb;
    s->sink_input->moving = sink_input_moving_cb;
    s->sink_input->suspend = sink_input_suspend_cb;
    s->sink_input->send_event = sink_input_send_event_cb;
    s->sink_input->userdata = s;

    start_index = ssync ? pa_memblockq_get_read_index(ssync->memblockq) : 0;

    fix_playback_buffer_attr(s);

    pa_sink_input_get_silence(sink_input, &silence);
    memblockq_name = pa_sprintf_malloc("native protocol playback stream memblockq [%u]", s->sink_input->index);
    s->memblockq = pa_memblockq_new(
            memblockq_name,
            start_index,
            s->buffer_attr.maxlength,
            s->buffer_attr.tlength,
            &sink_input->sample_spec,
            s->buffer_attr.prebuf,
            s->buffer_attr.minreq,
            0,
            &silence);
    pa_xfree(memblockq_name);
    pa_memblock_unref(silence.memblock);

    pa_memblockq_get_attr(s->memblockq, &s->buffer_attr);

    *missing = (uint32_t) pa_memblockq_pop_missing(s->memblockq);

#ifdef PROTOCOL_NATIVE_DEBUG
    pa_log("missing original: %li", (long int) *missing);
#endif

    *ss = s->sink_input->sample_spec;
    *map = s->sink_input->channel_map;

    pa_idxset_put(c->output_streams, s, &s->index);

    pa_log_info("Final latency %0.2f ms = %0.2f ms + 2*%0.2f ms + %0.2f ms",
                ((double) pa_bytes_to_usec(s->buffer_attr.tlength, &sink_input->sample_spec) + (double) s->configured_sink_latency) / PA_USEC_PER_MSEC,
                (double) pa_bytes_to_usec(s->buffer_attr.tlength-s->buffer_attr.minreq*2, &sink_input->sample_spec) / PA_USEC_PER_MSEC,
                (double) pa_bytes_to_usec(s->buffer_attr.minreq, &sink_input->sample_spec) / PA_USEC_PER_MSEC,
                (double) s->configured_sink_latency / PA_USEC_PER_MSEC);

    pa_sink_input_put(s->sink_input);

out:
    if (formats)
        pa_idxset_free(formats, (pa_free_cb_t) pa_format_info_free);

    return s;
}

/* Called from IO context */
static void playback_stream_request_bytes(playback_stream *s) {
    size_t m;

    playback_stream_assert_ref(s);

    m = pa_memblockq_pop_missing(s->memblockq);

    /* pa_log("request_bytes(%lu) (tlength=%lu minreq=%lu length=%lu really missing=%lli)", */
    /*        (unsigned long) m, */
    /*        pa_memblockq_get_tlength(s->memblockq), */
    /*        pa_memblockq_get_minreq(s->memblockq), */
    /*        pa_memblockq_get_length(s->memblockq), */
    /*        (long long) pa_memblockq_get_tlength(s->memblockq) - (long long) pa_memblockq_get_length(s->memblockq)); */

    if (m <= 0)
        return;

#ifdef PROTOCOL_NATIVE_DEBUG
    pa_log("request_bytes(%lu)", (unsigned long) m);
#endif

    if (pa_atomic_add(&s->missing, (int) m) <= 0)
        pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(s), PLAYBACK_STREAM_MESSAGE_REQUEST_DATA, NULL, 0, NULL, NULL);
}

/* Called from main context */
static void playback_stream_send_killed(playback_stream *p) {
    pa_tagstruct *t;
    playback_stream_assert_ref(p);

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_PLAYBACK_STREAM_KILLED);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_putu32(t, p->index);
    pa_pstream_send_tagstruct(p->connection->pstream, t);
}

/* Called from main context */
static int native_connection_process_msg(pa_msgobject *o, int code, void*userdata, int64_t offset, pa_memchunk *chunk) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(o);
    pa_native_connection_assert_ref(c);

    if (!c->protocol)
        return -1;

    switch (code) {

        case CONNECTION_MESSAGE_REVOKE:
            pa_pstream_send_revoke(c->pstream, PA_PTR_TO_UINT(userdata));
            break;

        case CONNECTION_MESSAGE_RELEASE:
            pa_pstream_send_release(c->pstream, PA_PTR_TO_UINT(userdata));
            break;
    }

    return 0;
}

/* Called from main context */
static void native_connection_unlink(pa_native_connection *c) {
    record_stream *r;
    output_stream *o;

    pa_assert(c);

    if (!c->protocol)
        return;

    pa_hook_fire(&c->protocol->hooks[PA_NATIVE_HOOK_CONNECTION_UNLINK], c);

    if (c->options)
        pa_native_options_unref(c->options);

    if (c->srbpending)
        pa_srbchannel_free(c->srbpending);

    while ((r = pa_idxset_first(c->record_streams, NULL)))
        record_stream_unlink(r);

    while ((o = pa_idxset_first(c->output_streams, NULL)))
        if (playback_stream_isinstance(o))
            playback_stream_unlink(PLAYBACK_STREAM(o));
        else
            upload_stream_unlink(UPLOAD_STREAM(o));

    if (c->subscription)
        pa_subscription_free(c->subscription);

    if (c->pstream)
        pa_pstream_unlink(c->pstream);

    if (c->auth_timeout_event) {
        c->protocol->core->mainloop->time_free(c->auth_timeout_event);
        c->auth_timeout_event = NULL;
    }

    pa_assert_se(pa_idxset_remove_by_data(c->protocol->connections, c, NULL) == c);
    c->protocol = NULL;
    pa_native_connection_unref(c);
}

/* Called from main context */
static void native_connection_free(pa_object *o) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(o);

    pa_assert(c);

    native_connection_unlink(c);

    pa_idxset_free(c->record_streams, NULL);
    pa_idxset_free(c->output_streams, NULL);

    pa_pdispatch_unref(c->pdispatch);
    pa_pstream_unref(c->pstream);
    if (c->rw_mempool)
        pa_mempool_unref(c->rw_mempool);

    pa_client_free(c->client);

    pa_xfree(c);
}

/* Called from main context */
static void native_connection_send_memblock(pa_native_connection *c) {
    uint32_t start;
    record_stream *r;

    start = PA_IDXSET_INVALID;
    for (;;) {
        pa_memchunk chunk;

        if (!(r = RECORD_STREAM(pa_idxset_rrobin(c->record_streams, &c->rrobin_index))))
            return;

        if (start == PA_IDXSET_INVALID)
            start = c->rrobin_index;
        else if (start == c->rrobin_index)
            return;

        if (pa_memblockq_peek(r->memblockq, &chunk) >= 0) {
            pa_memchunk schunk = chunk;

            if (schunk.length > r->buffer_attr.fragsize)
                schunk.length = r->buffer_attr.fragsize;

            pa_pstream_send_memblock(c->pstream, r->index, 0, PA_SEEK_RELATIVE, &schunk);

            pa_memblockq_drop(r->memblockq, schunk.length);
            pa_memblock_unref(schunk.memblock);

            return;
        }
    }
}

/*** sink input callbacks ***/

/* Called from thread context */
static void handle_seek(playback_stream *s, int64_t indexw) {
    playback_stream_assert_ref(s);

/*     pa_log("handle_seek: %llu -- %i", (unsigned long long) s->sink_input->thread_info.underrun_for, pa_memblockq_is_readable(s->memblockq)); */

    if (s->sink_input->thread_info.underrun_for > 0) {

/*         pa_log("%lu vs. %lu", (unsigned long) pa_memblockq_get_length(s->memblockq), (unsigned long) pa_memblockq_get_prebuf(s->memblockq)); */

        if (pa_memblockq_is_readable(s->memblockq)) {

            /* We just ended an underrun, let's ask the sink
             * for a complete rewind rewrite */

            pa_log_debug("Requesting rewind due to end of underrun.");
            pa_sink_input_request_rewind(s->sink_input,
                                         (size_t) (s->sink_input->thread_info.underrun_for == (uint64_t) -1 ? 0 :
                                                   s->sink_input->thread_info.underrun_for),
                                         false, true, false);
        }

    } else {
        int64_t indexr;

        indexr = pa_memblockq_get_read_index(s->memblockq);

        if (indexw < indexr) {
            /* OK, the sink already asked for this data, so
             * let's have it ask us again */

            pa_log_debug("Requesting rewind due to rewrite.");
            pa_sink_input_request_rewind(s->sink_input, (size_t) (indexr - indexw), true, false, false);
        }
    }

    playback_stream_request_bytes(s);
}

static void flush_write_no_account(pa_memblockq *q) {
    pa_memblockq_flush_write(q, false);
}

/* Called from thread context */
static int sink_input_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk) {
    pa_sink_input *i = PA_SINK_INPUT(o);
    playback_stream *s;

    pa_sink_input_assert_ref(i);
    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);

    switch (code) {

        case SINK_INPUT_MESSAGE_SEEK:
        case SINK_INPUT_MESSAGE_POST_DATA: {
            int64_t windex = pa_memblockq_get_write_index(s->memblockq);

            if (code == SINK_INPUT_MESSAGE_SEEK) {
                /* The client side is incapable of accounting correctly
                 * for seeks of a type != PA_SEEK_RELATIVE. We need to be
                 * able to deal with that. */

                pa_memblockq_seek(s->memblockq, offset, PA_PTR_TO_UINT(userdata), PA_PTR_TO_UINT(userdata) == PA_SEEK_RELATIVE);
                windex = PA_MIN(windex, pa_memblockq_get_write_index(s->memblockq));
            }

            if (chunk && pa_memblockq_push_align(s->memblockq, chunk) < 0) {
                if (pa_log_ratelimit(PA_LOG_WARN))
                    pa_log_warn("Failed to push data into queue");
                pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(s), PLAYBACK_STREAM_MESSAGE_OVERFLOW, NULL, 0, NULL, NULL);
                pa_memblockq_seek(s->memblockq, (int64_t) chunk->length, PA_SEEK_RELATIVE, true);
            }

            /* If more data is in queue, we rewind later instead. */
            if (s->seek_windex != -1)
                windex = PA_MIN(windex, s->seek_windex);
            if (pa_atomic_dec(&s->seek_or_post_in_queue) > 1)
                s->seek_windex = windex;
            else {
                s->seek_windex = -1;
                handle_seek(s, windex);
            }
            return 0;
        }

        case SINK_INPUT_MESSAGE_DRAIN:
        case SINK_INPUT_MESSAGE_FLUSH:
        case SINK_INPUT_MESSAGE_PREBUF_FORCE:
        case SINK_INPUT_MESSAGE_TRIGGER: {

            int64_t windex;
            pa_sink_input *isync;
            void (*func)(pa_memblockq *bq);

            switch (code) {
                case SINK_INPUT_MESSAGE_FLUSH:
                    func = flush_write_no_account;
                    break;

                case SINK_INPUT_MESSAGE_PREBUF_FORCE:
                    func = pa_memblockq_prebuf_force;
                    break;

                case SINK_INPUT_MESSAGE_DRAIN:
                case SINK_INPUT_MESSAGE_TRIGGER:
                    func = pa_memblockq_prebuf_disable;
                    break;

                default:
                    pa_assert_not_reached();
            }

            windex = pa_memblockq_get_write_index(s->memblockq);
            func(s->memblockq);
            handle_seek(s, windex);

            /* Do the same for all other members in the sync group */
            for (isync = i->sync_prev; isync; isync = isync->sync_prev) {
                playback_stream *ssync = PLAYBACK_STREAM(isync->userdata);
                windex = pa_memblockq_get_write_index(ssync->memblockq);
                func(ssync->memblockq);
                handle_seek(ssync, windex);
            }

            for (isync = i->sync_next; isync; isync = isync->sync_next) {
                playback_stream *ssync = PLAYBACK_STREAM(isync->userdata);
                windex = pa_memblockq_get_write_index(ssync->memblockq);
                func(ssync->memblockq);
                handle_seek(ssync, windex);
            }

            if (code == SINK_INPUT_MESSAGE_DRAIN) {
                if (!pa_memblockq_is_readable(s->memblockq))
                    pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(s), PLAYBACK_STREAM_MESSAGE_DRAIN_ACK, userdata, 0, NULL, NULL);
                else {
                    s->drain_tag = PA_PTR_TO_UINT(userdata);
                    s->drain_request = true;
                }
            }

            return 0;
        }

        case SINK_INPUT_MESSAGE_UPDATE_LATENCY:
            /* Atomically get a snapshot of all timing parameters... */
            s->read_index = pa_memblockq_get_read_index(s->memblockq);
            s->write_index = pa_memblockq_get_write_index(s->memblockq);
            s->render_memblockq_length = pa_memblockq_get_length(s->sink_input->thread_info.render_memblockq);
            s->current_sink_latency = pa_sink_get_latency_within_thread(s->sink_input->sink, false);
            s->underrun_for = s->sink_input->thread_info.underrun_for;
            s->playing_for = s->sink_input->thread_info.playing_for;

            return 0;

        case PA_SINK_INPUT_MESSAGE_SET_STATE: {
            int64_t windex;

            windex = pa_memblockq_get_write_index(s->memblockq);

            /* We enable prebuffering so that after CORKED -> RUNNING
             * transitions we don't have trouble with underruns in case the
             * buffer has too little data. This must not be done when draining
             * has been requested, however, otherwise the buffered audio would
             * never play. */
            if (!s->drain_request)
                pa_memblockq_prebuf_force(s->memblockq);

            handle_seek(s, windex);

            /* Fall through to the default handler */
            break;
        }

        case PA_SINK_INPUT_MESSAGE_GET_LATENCY: {
            pa_usec_t *r = userdata;

            *r = pa_bytes_to_usec(pa_memblockq_get_length(s->memblockq), &i->sample_spec);

            /* Fall through, the default handler will add in the extra
             * latency added by the resampler */
            break;
        }

        case SINK_INPUT_MESSAGE_UPDATE_BUFFER_ATTR: {
            pa_memblockq_apply_attr(s->memblockq, &s->buffer_attr);
            pa_memblockq_get_attr(s->memblockq, &s->buffer_attr);
            return 0;
        }
    }

    return pa_sink_input_process_msg(o, code, userdata, offset, chunk);
}

static bool handle_input_underrun(playback_stream *s, bool force) {
    bool send_drain;

    if (pa_memblockq_is_readable(s->memblockq))
        return false;

    if (!s->is_underrun)
        pa_log_debug("%s %s of '%s'", force ? "Actual" : "Implicit",
            s->drain_request ? "drain" : "underrun", pa_strnull(pa_proplist_gets(s->sink_input->proplist, PA_PROP_MEDIA_NAME)));

    send_drain = s->drain_request && (force || pa_sink_input_safe_to_remove(s->sink_input));

    if (send_drain) {
         s->drain_request = false;
         pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(s), PLAYBACK_STREAM_MESSAGE_DRAIN_ACK, PA_UINT_TO_PTR(s->drain_tag), 0, NULL, NULL);
         pa_log_debug("Drain acknowledged of '%s'", pa_strnull(pa_proplist_gets(s->sink_input->proplist, PA_PROP_MEDIA_NAME)));
    } else if (!s->is_underrun) {
         pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(s), PLAYBACK_STREAM_MESSAGE_UNDERFLOW, NULL, pa_memblockq_get_read_index(s->memblockq), NULL, NULL);
    }
    s->is_underrun = true;
    playback_stream_request_bytes(s);
    return true;
}

/* Called from thread context */
static bool sink_input_process_underrun_cb(pa_sink_input *i) {
    playback_stream *s;

    pa_sink_input_assert_ref(i);
    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);

    return handle_input_underrun(s, true);
}

/* Called from thread context */
static int sink_input_pop_cb(pa_sink_input *i, size_t nbytes, pa_memchunk *chunk) {
    playback_stream *s;

    pa_sink_input_assert_ref(i);
    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);
    pa_assert(chunk);

#ifdef PROTOCOL_NATIVE_DEBUG
    pa_log("%s, pop(): %lu", pa_proplist_gets(i->proplist, PA_PROP_MEDIA_NAME), (unsigned long) pa_memblockq_get_length(s->memblockq));
#endif

    if (!handle_input_underrun(s, false))
        s->is_underrun = false;

    /* This call will not fail with prebuf=0, hence we check for
       underrun explicitly in handle_input_underrun */
    if (pa_memblockq_peek(s->memblockq, chunk) < 0)
        return -1;

    chunk->length = PA_MIN(nbytes, chunk->length);

    if (i->thread_info.underrun_for > 0)
        pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(s), PLAYBACK_STREAM_MESSAGE_STARTED, NULL, 0, NULL, NULL);

    pa_memblockq_drop(s->memblockq, chunk->length);
    playback_stream_request_bytes(s);

    return 0;
}

/* Called from thread context */
static void sink_input_process_rewind_cb(pa_sink_input *i, size_t nbytes) {
    playback_stream *s;

    pa_sink_input_assert_ref(i);
    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);

    /* If we are in an underrun, then we don't rewind */
    if (i->thread_info.underrun_for > 0)
        return;

    pa_memblockq_rewind(s->memblockq, nbytes);
}

/* Called from thread context */
static void sink_input_update_max_rewind_cb(pa_sink_input *i, size_t nbytes) {
    playback_stream *s;

    pa_sink_input_assert_ref(i);
    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);

    pa_memblockq_set_maxrewind(s->memblockq, nbytes);
}

/* Called from thread context */
static void sink_input_update_max_request_cb(pa_sink_input *i, size_t nbytes) {
    playback_stream *s;
    size_t new_tlength, old_tlength;

    pa_sink_input_assert_ref(i);
    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);

    old_tlength = pa_memblockq_get_tlength(s->memblockq);
    new_tlength = nbytes+2*pa_memblockq_get_minreq(s->memblockq);

    if (old_tlength < new_tlength) {
        pa_log_debug("max_request changed, trying to update from %zu to %zu.", old_tlength, new_tlength);
        pa_memblockq_set_tlength(s->memblockq, new_tlength);
        new_tlength = pa_memblockq_get_tlength(s->memblockq);

        if (new_tlength == old_tlength)
            pa_log_debug("Failed to increase tlength");
        else {
            pa_log_debug("Notifying client about increased tlength");
            pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(s), PLAYBACK_STREAM_MESSAGE_UPDATE_TLENGTH, NULL, pa_memblockq_get_tlength(s->memblockq), NULL, NULL);
        }
    }
}

/* Called from main context */
static void sink_input_kill_cb(pa_sink_input *i) {
    playback_stream *s;

    pa_sink_input_assert_ref(i);
    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);

    playback_stream_send_killed(s);
    playback_stream_unlink(s);
}

/* Called from main context */
static void sink_input_send_event_cb(pa_sink_input *i, const char *event, pa_proplist *pl) {
    playback_stream *s;
    pa_tagstruct *t;

    pa_sink_input_assert_ref(i);
    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);

    if (s->connection->version < 15)
      return;

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_PLAYBACK_STREAM_EVENT);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_putu32(t, s->index);
    pa_tagstruct_puts(t, event);
    pa_tagstruct_put_proplist(t, pl);
    pa_pstream_send_tagstruct(s->connection->pstream, t);
}

/* Called from main context */
static void sink_input_suspend_cb(pa_sink_input *i, pa_sink_state_t old_state, pa_suspend_cause_t old_suspend_cause) {
    playback_stream *s;
    pa_tagstruct *t;
    bool suspend;

    pa_sink_input_assert_ref(i);

    /* State has not changed, nothing to do */
    if (old_state == i->sink->state)
        return;

    suspend = (i->sink->state == PA_SINK_SUSPENDED);

    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);

    if (s->connection->version < 12)
      return;

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_PLAYBACK_STREAM_SUSPENDED);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_putu32(t, s->index);
    pa_tagstruct_put_boolean(t, suspend);
    pa_pstream_send_tagstruct(s->connection->pstream, t);
}

/* Called from main context */
static void sink_input_moving_cb(pa_sink_input *i, pa_sink *dest) {
    playback_stream *s;
    pa_tagstruct *t;

    pa_sink_input_assert_ref(i);
    s = PLAYBACK_STREAM(i->userdata);
    playback_stream_assert_ref(s);

    if (!dest)
        return;

    fix_playback_buffer_attr(s);
    pa_memblockq_apply_attr(s->memblockq, &s->buffer_attr);
    pa_memblockq_get_attr(s->memblockq, &s->buffer_attr);

    if (s->connection->version < 12)
      return;

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_PLAYBACK_STREAM_MOVED);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_putu32(t, s->index);
    pa_tagstruct_putu32(t, dest->index);
    pa_tagstruct_puts(t, dest->name);
    pa_tagstruct_put_boolean(t, dest->state == PA_SINK_SUSPENDED);

    if (s->connection->version >= 13) {
        pa_tagstruct_putu32(t, s->buffer_attr.maxlength);
        pa_tagstruct_putu32(t, s->buffer_attr.tlength);
        pa_tagstruct_putu32(t, s->buffer_attr.prebuf);
        pa_tagstruct_putu32(t, s->buffer_attr.minreq);
        pa_tagstruct_put_usec(t, s->configured_sink_latency);
    }

    pa_pstream_send_tagstruct(s->connection->pstream, t);
}

/*** source_output callbacks ***/

/* Called from thread context */
static int source_output_process_msg(pa_msgobject *_o, int code, void *userdata, int64_t offset, pa_memchunk *chunk) {
    pa_source_output *o = PA_SOURCE_OUTPUT(_o);
    record_stream *s;

    pa_source_output_assert_ref(o);
    s = RECORD_STREAM(o->userdata);
    record_stream_assert_ref(s);

    switch (code) {
        case SOURCE_OUTPUT_MESSAGE_UPDATE_LATENCY:
            /* Atomically get a snapshot of all timing parameters... */
            s->current_monitor_latency = o->source->monitor_of ? pa_sink_get_latency_within_thread(o->source->monitor_of, false) : 0;
            s->current_source_latency = pa_source_get_latency_within_thread(o->source, false);
            s->on_the_fly_snapshot = pa_atomic_load(&s->on_the_fly);
            return 0;
    }

    return pa_source_output_process_msg(_o, code, userdata, offset, chunk);
}

/* Called from thread context */
static void source_output_push_cb(pa_source_output *o, const pa_memchunk *chunk) {
    record_stream *s;

    pa_source_output_assert_ref(o);
    s = RECORD_STREAM(o->userdata);
    record_stream_assert_ref(s);
    pa_assert(chunk);

    pa_atomic_add(&s->on_the_fly, chunk->length);
    pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(s), RECORD_STREAM_MESSAGE_POST_DATA, NULL, 0, chunk, NULL);
}

static void source_output_kill_cb(pa_source_output *o) {
    record_stream *s;

    pa_source_output_assert_ref(o);
    s = RECORD_STREAM(o->userdata);
    record_stream_assert_ref(s);

    record_stream_send_killed(s);
    record_stream_unlink(s);
}

static pa_usec_t source_output_get_latency_cb(pa_source_output *o) {
    record_stream *s;

    pa_source_output_assert_ref(o);
    s = RECORD_STREAM(o->userdata);
    record_stream_assert_ref(s);

    /*pa_log("get_latency: %u", pa_memblockq_get_length(s->memblockq));*/

    return pa_bytes_to_usec(pa_memblockq_get_length(s->memblockq), &o->sample_spec);
}

/* Called from main context */
static void source_output_send_event_cb(pa_source_output *o, const char *event, pa_proplist *pl) {
    record_stream *s;
    pa_tagstruct *t;

    pa_source_output_assert_ref(o);
    s = RECORD_STREAM(o->userdata);
    record_stream_assert_ref(s);

    if (s->connection->version < 15)
      return;

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_RECORD_STREAM_EVENT);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_putu32(t, s->index);
    pa_tagstruct_puts(t, event);
    pa_tagstruct_put_proplist(t, pl);
    pa_pstream_send_tagstruct(s->connection->pstream, t);
}

/* Called from main context */
static void source_output_suspend_cb(pa_source_output *o, pa_source_state_t old_state, pa_suspend_cause_t old_suspend_cause) {
    record_stream *s;
    pa_tagstruct *t;
    bool suspend;

    pa_source_output_assert_ref(o);

    /* State has not changed, nothing to do */
    if (old_state == o->source->state)
        return;

    suspend = (o->source->state == PA_SOURCE_SUSPENDED);

    s = RECORD_STREAM(o->userdata);
    record_stream_assert_ref(s);

    if (s->connection->version < 12)
      return;

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_RECORD_STREAM_SUSPENDED);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_putu32(t, s->index);
    pa_tagstruct_put_boolean(t, suspend);
    pa_pstream_send_tagstruct(s->connection->pstream, t);
}

/* Called from main context */
static void source_output_moving_cb(pa_source_output *o, pa_source *dest) {
    record_stream *s;
    pa_tagstruct *t;

    pa_source_output_assert_ref(o);
    s = RECORD_STREAM(o->userdata);
    record_stream_assert_ref(s);

    if (!dest)
        return;

    fix_record_buffer_attr_pre(s);
    pa_memblockq_set_maxlength(s->memblockq, s->buffer_attr.maxlength);
    pa_memblockq_get_attr(s->memblockq, &s->buffer_attr);
    fix_record_buffer_attr_post(s);

    if (s->connection->version < 12)
      return;

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_RECORD_STREAM_MOVED);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_putu32(t, s->index);
    pa_tagstruct_putu32(t, dest->index);
    pa_tagstruct_puts(t, dest->name);
    pa_tagstruct_put_boolean(t, dest->state == PA_SOURCE_SUSPENDED);

    if (s->connection->version >= 13) {
        pa_tagstruct_putu32(t, s->buffer_attr.maxlength);
        pa_tagstruct_putu32(t, s->buffer_attr.fragsize);
        pa_tagstruct_put_usec(t, s->configured_source_latency);
    }

    pa_pstream_send_tagstruct(s->connection->pstream, t);
}

/*** pdispatch callbacks ***/

static void protocol_error(pa_native_connection *c) {
    pa_log("protocol error, kicking client");
    native_connection_unlink(c);
}

#define CHECK_VALIDITY(pstream, expression, tag, error) do { \
if (!(expression)) { \
    pa_pstream_send_error((pstream), (tag), (error)); \
    return; \
} \
} while(0);

#define CHECK_VALIDITY_GOTO(pstream, expression, tag, error, label) do { \
if (!(expression)) { \
    pa_pstream_send_error((pstream), (tag), (error)); \
    goto label; \
} \
} while(0);

static pa_tagstruct *reply_new(uint32_t tag) {
    pa_tagstruct *reply;

    reply = pa_tagstruct_new();
    pa_tagstruct_putu32(reply, PA_COMMAND_REPLY);
    pa_tagstruct_putu32(reply, tag);
    return reply;
}

static void command_create_playback_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    playback_stream *s;
    uint32_t sink_index, syncid, missing = 0;
    pa_buffer_attr attr;
    const char *name = NULL, *sink_name;
    pa_sample_spec ss;
    pa_channel_map map;
    pa_tagstruct *reply;
    pa_sink *sink = NULL;
    pa_cvolume volume;
    bool
        corked = false,
        no_remap = false,
        no_remix = false,
        fix_format = false,
        fix_rate = false,
        fix_channels = false,
        no_move = false,
        variable_rate = false,
        muted = false,
        adjust_latency = false,
        early_requests = false,
        dont_inhibit_auto_suspend = false,
        volume_set = true,
        muted_set = false,
        fail_on_suspend = false,
        relative_volume = false,
        passthrough = false;

    pa_sink_input_flags_t flags = 0;
    pa_proplist *p = NULL;
    int ret = PA_ERR_INVALID;
    uint8_t n_formats = 0;
    pa_format_info *format;
    pa_idxset *formats = NULL;
    uint32_t i;

    pa_native_connection_assert_ref(c);
    pa_assert(t);
    memset(&attr, 0, sizeof(attr));

    if ((c->version < 13 && (pa_tagstruct_gets(t, &name) < 0 || !name)) ||
        pa_tagstruct_get(
                t,
                PA_TAG_SAMPLE_SPEC, &ss,
                PA_TAG_CHANNEL_MAP, &map,
                PA_TAG_U32, &sink_index,
                PA_TAG_STRING, &sink_name,
                PA_TAG_U32, &attr.maxlength,
                PA_TAG_BOOLEAN, &corked,
                PA_TAG_U32, &attr.tlength,
                PA_TAG_U32, &attr.prebuf,
                PA_TAG_U32, &attr.minreq,
                PA_TAG_U32, &syncid,
                PA_TAG_CVOLUME, &volume,
                PA_TAG_INVALID) < 0) {

        protocol_error(c);
        goto finish;
    }

    CHECK_VALIDITY_GOTO(c->pstream, c->authorized, tag, PA_ERR_ACCESS, finish);
    CHECK_VALIDITY_GOTO(c->pstream, !sink_name || pa_namereg_is_valid_name_or_wildcard(sink_name, PA_NAMEREG_SINK), tag, PA_ERR_INVALID, finish);
    CHECK_VALIDITY_GOTO(c->pstream, sink_index == PA_INVALID_INDEX || !sink_name, tag, PA_ERR_INVALID, finish);
    CHECK_VALIDITY_GOTO(c->pstream, !sink_name || sink_index == PA_INVALID_INDEX, tag, PA_ERR_INVALID, finish);
    CHECK_VALIDITY_GOTO(c->pstream, pa_cvolume_valid(&volume), tag, PA_ERR_INVALID, finish);

    p = pa_proplist_new();

    if (name)
        pa_proplist_sets(p, PA_PROP_MEDIA_NAME, name);

    if (c->version >= 12) {
        /* Since 0.9.8 the user can ask for a couple of additional flags */

        if (pa_tagstruct_get_boolean(t, &no_remap) < 0 ||
            pa_tagstruct_get_boolean(t, &no_remix) < 0 ||
            pa_tagstruct_get_boolean(t, &fix_format) < 0 ||
            pa_tagstruct_get_boolean(t, &fix_rate) < 0 ||
            pa_tagstruct_get_boolean(t, &fix_channels) < 0 ||
            pa_tagstruct_get_boolean(t, &no_move) < 0 ||
            pa_tagstruct_get_boolean(t, &variable_rate) < 0) {

            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 13) {

        if (pa_tagstruct_get_boolean(t, &muted) < 0 ||
            pa_tagstruct_get_boolean(t, &adjust_latency) < 0 ||
            pa_tagstruct_get_proplist(t, p) < 0) {

            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 14) {

        if (pa_tagstruct_get_boolean(t, &volume_set) < 0 ||
            pa_tagstruct_get_boolean(t, &early_requests) < 0) {

            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 15) {

        if (pa_tagstruct_get_boolean(t, &muted_set) < 0 ||
            pa_tagstruct_get_boolean(t, &dont_inhibit_auto_suspend) < 0 ||
            pa_tagstruct_get_boolean(t, &fail_on_suspend) < 0) {

            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 17) {

        if (pa_tagstruct_get_boolean(t, &relative_volume) < 0) {

            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 18) {

        if (pa_tagstruct_get_boolean(t, &passthrough) < 0 ) {
            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 21) {

        if (pa_tagstruct_getu8(t, &n_formats) < 0) {
            protocol_error(c);
            goto finish;
        }

        if (n_formats)
            formats = pa_idxset_new(NULL, NULL);

        for (i = 0; i < n_formats; i++) {
            format = pa_format_info_new();
            if (pa_tagstruct_get_format_info(t, format) < 0) {
                protocol_error(c);
                goto finish;
            }
            pa_idxset_put(formats, format, NULL);
        }
    }

    if (n_formats == 0) {
        CHECK_VALIDITY_GOTO(c->pstream, pa_sample_spec_valid(&ss), tag, PA_ERR_INVALID, finish);
        CHECK_VALIDITY_GOTO(c->pstream, map.channels == ss.channels && volume.channels == ss.channels, tag, PA_ERR_INVALID, finish);
        CHECK_VALIDITY_GOTO(c->pstream, pa_channel_map_valid(&map), tag, PA_ERR_INVALID, finish);
    } else {
        PA_IDXSET_FOREACH(format, formats, i) {
            CHECK_VALIDITY_GOTO(c->pstream, pa_format_info_valid(format), tag, PA_ERR_INVALID, finish);
        }
    }

    if (!pa_tagstruct_eof(t)) {
        protocol_error(c);
        goto finish;
    }

    if (sink_index != PA_INVALID_INDEX) {

        if (!(sink = pa_idxset_get_by_index(c->protocol->core->sinks, sink_index))) {
            pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
            goto finish;
        }

    } else if (sink_name) {

        if (!(sink = pa_namereg_get(c->protocol->core, sink_name, PA_NAMEREG_SINK))) {
            pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
            goto finish;
        }
    }

    flags =
        (corked ? PA_SINK_INPUT_START_CORKED : 0) |
        (no_remap ? PA_SINK_INPUT_NO_REMAP : 0) |
        (no_remix ? PA_SINK_INPUT_NO_REMIX : 0) |
        (fix_format ? PA_SINK_INPUT_FIX_FORMAT : 0) |
        (fix_rate ? PA_SINK_INPUT_FIX_RATE : 0) |
        (fix_channels ? PA_SINK_INPUT_FIX_CHANNELS : 0) |
        (no_move ? PA_SINK_INPUT_DONT_MOVE : 0) |
        (variable_rate ? PA_SINK_INPUT_VARIABLE_RATE : 0) |
        (dont_inhibit_auto_suspend ? PA_SINK_INPUT_DONT_INHIBIT_AUTO_SUSPEND : 0) |
        (fail_on_suspend ? PA_SINK_INPUT_NO_CREATE_ON_SUSPEND|PA_SINK_INPUT_KILL_ON_SUSPEND : 0) |
        (passthrough ? PA_SINK_INPUT_PASSTHROUGH : 0);

    /* Only since protocol version 15 there's a separate muted_set
     * flag. For older versions we synthesize it here */
    muted_set = muted_set || muted;

    s = playback_stream_new(c, sink, &ss, &map, formats, &attr, volume_set ? &volume : NULL, muted, muted_set, flags, p, adjust_latency, early_requests, relative_volume, syncid, &missing, &ret);
    /* We no longer own the formats idxset */
    formats = NULL;

    CHECK_VALIDITY_GOTO(c->pstream, s, tag, ret, finish);

    reply = reply_new(tag);
    pa_tagstruct_putu32(reply, s->index);
    pa_assert(s->sink_input);
    pa_tagstruct_putu32(reply, s->sink_input->index);
    pa_tagstruct_putu32(reply, missing);

#ifdef PROTOCOL_NATIVE_DEBUG
    pa_log("initial request is %u", missing);
#endif

    if (c->version >= 9) {
        /* Since 0.9.0 we support sending the buffer metrics back to the client */

        pa_tagstruct_putu32(reply, (uint32_t) s->buffer_attr.maxlength);
        pa_tagstruct_putu32(reply, (uint32_t) s->buffer_attr.tlength);
        pa_tagstruct_putu32(reply, (uint32_t) s->buffer_attr.prebuf);
        pa_tagstruct_putu32(reply, (uint32_t) s->buffer_attr.minreq);
    }

    if (c->version >= 12) {
        /* Since 0.9.8 we support sending the chosen sample
         * spec/channel map/device/suspend status back to the
         * client */

        pa_tagstruct_put_sample_spec(reply, &ss);
        pa_tagstruct_put_channel_map(reply, &map);

        pa_tagstruct_putu32(reply, s->sink_input->sink->index);
        pa_tagstruct_puts(reply, s->sink_input->sink->name);

        pa_tagstruct_put_boolean(reply, s->sink_input->sink->state == PA_SINK_SUSPENDED);
    }

    if (c->version >= 13)
        pa_tagstruct_put_usec(reply, s->configured_sink_latency);

    if (c->version >= 21) {
        /* Send back the format we negotiated */
        if (s->sink_input->format)
            pa_tagstruct_put_format_info(reply, s->sink_input->format);
        else {
            pa_format_info *f = pa_format_info_new();
            pa_tagstruct_put_format_info(reply, f);
            pa_format_info_free(f);
        }
    }

    pa_pstream_send_tagstruct(c->pstream, reply);

finish:
    if (p)
        pa_proplist_free(p);
    if (formats)
        pa_idxset_free(formats, (pa_free_cb_t) pa_format_info_free);
}

static void command_delete_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t channel;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &channel) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    switch (command) {

        case PA_COMMAND_DELETE_PLAYBACK_STREAM: {
            playback_stream *s;
            if (!(s = pa_idxset_get_by_index(c->output_streams, channel)) || !playback_stream_isinstance(s)) {
                pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
                return;
            }

            playback_stream_unlink(s);
            break;
        }

        case PA_COMMAND_DELETE_RECORD_STREAM: {
            record_stream *s;
            if (!(s = pa_idxset_get_by_index(c->record_streams, channel))) {
                pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
                return;
            }

            record_stream_unlink(s);
            break;
        }

        case PA_COMMAND_DELETE_UPLOAD_STREAM: {
            upload_stream *s;

            if (!(s = pa_idxset_get_by_index(c->output_streams, channel)) || !upload_stream_isinstance(s)) {
                pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
                return;
            }

            upload_stream_unlink(s);
            break;
        }

        default:
            pa_assert_not_reached();
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_create_record_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    record_stream *s;
    pa_buffer_attr attr;
    uint32_t source_index;
    const char *name = NULL, *source_name;
    pa_sample_spec ss;
    pa_channel_map map;
    pa_tagstruct *reply;
    pa_source *source = NULL;
    pa_cvolume volume;
    bool
        corked = false,
        no_remap = false,
        no_remix = false,
        fix_format = false,
        fix_rate = false,
        fix_channels = false,
        no_move = false,
        variable_rate = false,
        muted = false,
        adjust_latency = false,
        peak_detect = false,
        early_requests = false,
        dont_inhibit_auto_suspend = false,
        volume_set = false,
        muted_set = false,
        fail_on_suspend = false,
        relative_volume = false,
        passthrough = false;

    pa_source_output_flags_t flags = 0;
    pa_proplist *p = NULL;
    uint32_t direct_on_input_idx = PA_INVALID_INDEX;
    pa_sink_input *direct_on_input = NULL;
    int ret = PA_ERR_INVALID;
    uint8_t n_formats = 0;
    pa_format_info *format;
    pa_idxset *formats = NULL;
    uint32_t i;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    memset(&attr, 0, sizeof(attr));

    if ((c->version < 13 && (pa_tagstruct_gets(t, &name) < 0 || !name)) ||
        pa_tagstruct_get_sample_spec(t, &ss) < 0 ||
        pa_tagstruct_get_channel_map(t, &map) < 0 ||
        pa_tagstruct_getu32(t, &source_index) < 0 ||
        pa_tagstruct_gets(t, &source_name) < 0 ||
        pa_tagstruct_getu32(t, &attr.maxlength) < 0 ||
        pa_tagstruct_get_boolean(t, &corked) < 0 ||
        pa_tagstruct_getu32(t, &attr.fragsize) < 0) {

        protocol_error(c);
        goto finish;
    }

    CHECK_VALIDITY_GOTO(c->pstream, c->authorized, tag, PA_ERR_ACCESS, finish);
    CHECK_VALIDITY_GOTO(c->pstream, !source_name || pa_namereg_is_valid_name_or_wildcard(source_name, PA_NAMEREG_SOURCE), tag, PA_ERR_INVALID, finish);
    CHECK_VALIDITY_GOTO(c->pstream, source_index == PA_INVALID_INDEX || !source_name, tag, PA_ERR_INVALID, finish);
    CHECK_VALIDITY_GOTO(c->pstream, !source_name || source_index == PA_INVALID_INDEX, tag, PA_ERR_INVALID, finish);

    p = pa_proplist_new();

    if (name)
        pa_proplist_sets(p, PA_PROP_MEDIA_NAME, name);

    if (c->version >= 12) {
        /* Since 0.9.8 the user can ask for a couple of additional flags */

        if (pa_tagstruct_get_boolean(t, &no_remap) < 0 ||
            pa_tagstruct_get_boolean(t, &no_remix) < 0 ||
            pa_tagstruct_get_boolean(t, &fix_format) < 0 ||
            pa_tagstruct_get_boolean(t, &fix_rate) < 0 ||
            pa_tagstruct_get_boolean(t, &fix_channels) < 0 ||
            pa_tagstruct_get_boolean(t, &no_move) < 0 ||
            pa_tagstruct_get_boolean(t, &variable_rate) < 0) {

            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 13) {

        if (pa_tagstruct_get_boolean(t, &peak_detect) < 0 ||
            pa_tagstruct_get_boolean(t, &adjust_latency) < 0 ||
            pa_tagstruct_get_proplist(t, p) < 0 ||
            pa_tagstruct_getu32(t, &direct_on_input_idx) < 0) {

            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 14) {

        if (pa_tagstruct_get_boolean(t, &early_requests) < 0) {
            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 15) {

        if (pa_tagstruct_get_boolean(t, &dont_inhibit_auto_suspend) < 0 ||
            pa_tagstruct_get_boolean(t, &fail_on_suspend) < 0) {

            protocol_error(c);
            goto finish;
        }
    }

    if (c->version >= 22) {
        /* For newer client versions (with per-source-output volumes), we try
         * to make the behaviour for playback and record streams the same. */
        volume_set = true;

        if (pa_tagstruct_getu8(t, &n_formats) < 0) {
            protocol_error(c);
            goto finish;
        }

        if (n_formats)
            formats = pa_idxset_new(NULL, NULL);

        for (i = 0; i < n_formats; i++) {
            format = pa_format_info_new();
            if (pa_tagstruct_get_format_info(t, format) < 0) {
                protocol_error(c);
                goto finish;
            }
            pa_idxset_put(formats, format, NULL);
        }

        if (pa_tagstruct_get_cvolume(t, &volume) < 0 ||
            pa_tagstruct_get_boolean(t, &muted) < 0 ||
            pa_tagstruct_get_boolean(t, &volume_set) < 0 ||
            pa_tagstruct_get_boolean(t, &muted_set) < 0 ||
            pa_tagstruct_get_boolean(t, &relative_volume) < 0 ||
            pa_tagstruct_get_boolean(t, &passthrough) < 0) {

            protocol_error(c);
            goto finish;
        }

        CHECK_VALIDITY_GOTO(c->pstream, pa_cvolume_valid(&volume), tag, PA_ERR_INVALID, finish);
    }

    if (n_formats == 0) {
        CHECK_VALIDITY_GOTO(c->pstream, pa_sample_spec_valid(&ss), tag, PA_ERR_INVALID, finish);
        CHECK_VALIDITY_GOTO(c->pstream, map.channels == ss.channels, tag, PA_ERR_INVALID, finish);
        CHECK_VALIDITY_GOTO(c->pstream, c->version < 22 || (volume.channels == ss.channels), tag, PA_ERR_INVALID, finish);
        CHECK_VALIDITY_GOTO(c->pstream, pa_channel_map_valid(&map), tag, PA_ERR_INVALID, finish);
    } else {
        PA_IDXSET_FOREACH(format, formats, i) {
            CHECK_VALIDITY_GOTO(c->pstream, pa_format_info_valid(format), tag, PA_ERR_INVALID, finish);
        }
    }

    if (!pa_tagstruct_eof(t)) {
        protocol_error(c);
        goto finish;
    }

    if (source_index != PA_INVALID_INDEX) {

        if (!(source = pa_idxset_get_by_index(c->protocol->core->sources, source_index))) {
            pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
            goto finish;
        }

    } else if (source_name) {

        if (!(source = pa_namereg_get(c->protocol->core, source_name, PA_NAMEREG_SOURCE))) {
            pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
            goto finish;
        }
    }

    if (direct_on_input_idx != PA_INVALID_INDEX) {

        if (!(direct_on_input = pa_idxset_get_by_index(c->protocol->core->sink_inputs, direct_on_input_idx))) {
            pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
            goto finish;
        }
    }

    flags =
        (corked ? PA_SOURCE_OUTPUT_START_CORKED : 0) |
        (no_remap ? PA_SOURCE_OUTPUT_NO_REMAP : 0) |
        (no_remix ? PA_SOURCE_OUTPUT_NO_REMIX : 0) |
        (fix_format ? PA_SOURCE_OUTPUT_FIX_FORMAT : 0) |
        (fix_rate ? PA_SOURCE_OUTPUT_FIX_RATE : 0) |
        (fix_channels ? PA_SOURCE_OUTPUT_FIX_CHANNELS : 0) |
        (no_move ? PA_SOURCE_OUTPUT_DONT_MOVE : 0) |
        (variable_rate ? PA_SOURCE_OUTPUT_VARIABLE_RATE : 0) |
        (dont_inhibit_auto_suspend ? PA_SOURCE_OUTPUT_DONT_INHIBIT_AUTO_SUSPEND : 0) |
        (fail_on_suspend ? PA_SOURCE_OUTPUT_NO_CREATE_ON_SUSPEND|PA_SOURCE_OUTPUT_KILL_ON_SUSPEND : 0) |
        (passthrough ? PA_SOURCE_OUTPUT_PASSTHROUGH : 0);

    s = record_stream_new(c, source, &ss, &map, formats, &attr, volume_set ? &volume : NULL, muted, muted_set, flags, p, adjust_latency, early_requests, relative_volume, peak_detect, direct_on_input, &ret);
    /* We no longer own the formats idxset */
    formats = NULL;

    CHECK_VALIDITY_GOTO(c->pstream, s, tag, ret, finish);

    reply = reply_new(tag);
    pa_tagstruct_putu32(reply, s->index);
    pa_assert(s->source_output);
    pa_tagstruct_putu32(reply, s->source_output->index);

    if (c->version >= 9) {
        /* Since 0.9 we support sending the buffer metrics back to the client */

        pa_tagstruct_putu32(reply, (uint32_t) s->buffer_attr.maxlength);
        pa_tagstruct_putu32(reply, (uint32_t) s->buffer_attr.fragsize);
    }

    if (c->version >= 12) {
        /* Since 0.9.8 we support sending the chosen sample
         * spec/channel map/device/suspend status back to the
         * client */

        pa_tagstruct_put_sample_spec(reply, &ss);
        pa_tagstruct_put_channel_map(reply, &map);

        pa_tagstruct_putu32(reply, s->source_output->source->index);
        pa_tagstruct_puts(reply, s->source_output->source->name);

        pa_tagstruct_put_boolean(reply, s->source_output->source->state == PA_SOURCE_SUSPENDED);
    }

    if (c->version >= 13)
        pa_tagstruct_put_usec(reply, s->configured_source_latency);

    if (c->version >= 22) {
        /* Send back the format we negotiated */
        if (s->source_output->format)
            pa_tagstruct_put_format_info(reply, s->source_output->format);
        else {
            pa_format_info *f = pa_format_info_new();
            pa_tagstruct_put_format_info(reply, f);
            pa_format_info_free(f);
        }
    }

    pa_pstream_send_tagstruct(c->pstream, reply);

finish:
    if (p)
        pa_proplist_free(p);
    if (formats)
        pa_idxset_free(formats, (pa_free_cb_t) pa_format_info_free);
}

static void command_exit(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    int ret;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (!pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    ret = pa_core_exit(c->protocol->core, false, 0);
    CHECK_VALIDITY(c->pstream, ret >= 0, tag, PA_ERR_ACCESS);

    pa_log_debug("Client %s asks us to terminate.", pa_strnull(pa_proplist_gets(c->client->proplist, PA_PROP_APPLICATION_PROCESS_BINARY)));

    pa_pstream_send_simple_ack(c->pstream, tag); /* nonsense */
}

static void setup_srbchannel(pa_native_connection *c, pa_mem_type_t shm_type) {
    pa_srbchannel_template srbt;
    pa_srbchannel *srb;
    pa_memchunk mc;
    pa_tagstruct *t;
    int fdlist[2];

#ifndef HAVE_CREDS
    pa_log_debug("Disabling srbchannel, reason: No fd passing support");
    return;
#endif

    if (!c->options->srbchannel) {
        pa_log_debug("Disabling srbchannel, reason: Must be enabled by module parameter");
        return;
    }

    if (c->version < 30) {
        pa_log_debug("Disabling srbchannel, reason: Protocol too old");
        return;
    }

    if (!pa_pstream_get_shm(c->pstream)) {
        pa_log_debug("Disabling srbchannel, reason: No SHM support");
        return;
    }

    if (c->rw_mempool) {
        pa_log_debug("Ignoring srbchannel setup, reason: received COMMAND_AUTH "
                     "more than once");
        return;
    }

    if (!(c->rw_mempool = pa_mempool_new(shm_type, c->protocol->core->shm_size, true))) {
        pa_log_warn("Disabling srbchannel, reason: Failed to allocate shared "
                    "writable memory pool.");
        return;
    }

    if (shm_type == PA_MEM_TYPE_SHARED_MEMFD) {
        const char *reason;
        if (pa_pstream_register_memfd_mempool(c->pstream, c->rw_mempool, &reason)) {
            pa_log_warn("Disabling srbchannel, reason: Failed to register memfd mempool: %s", reason);
            goto fail;
        }
    }
    pa_mempool_set_is_remote_writable(c->rw_mempool, true);

    srb = pa_srbchannel_new(c->protocol->core->mainloop, c->rw_mempool);
    if (!srb) {
        pa_log_debug("Failed to create srbchannel");
        goto fail;
    }
    pa_log_debug("Enabling srbchannel...");
    pa_srbchannel_export(srb, &srbt);

    /* Send enable command to client */
    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_ENABLE_SRBCHANNEL);
    pa_tagstruct_putu32(t, (size_t) srb); /* tag */
    fdlist[0] = srbt.readfd;
    fdlist[1] = srbt.writefd;
    pa_pstream_send_tagstruct_with_fds(c->pstream, t, 2, fdlist, false);

    /* Send ringbuffer memblock to client */
    mc.memblock = srbt.memblock;
    mc.index = 0;
    mc.length = pa_memblock_get_length(srbt.memblock);
    pa_pstream_send_memblock(c->pstream, 0, 0, 0, &mc);

    c->srbpending = srb;
    return;

fail:
    if (c->rw_mempool) {
        pa_mempool_unref(c->rw_mempool);
        c->rw_mempool = NULL;
    }
}

static void command_enable_srbchannel(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);

    if (tag != (uint32_t) (size_t) c->srbpending) {
        protocol_error(c);
        return;
    }

    pa_log_debug("Client enabled srbchannel.");
    pa_pstream_set_srbchannel(c->pstream, c->srbpending);
    c->srbpending = NULL;
}

static void command_auth(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    const void*cookie;
    bool memfd_on_remote = false, do_memfd = false;
    pa_tagstruct *reply;
    pa_mem_type_t shm_type;
    bool shm_on_remote = false, do_shm;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &c->version) < 0 ||
        pa_tagstruct_get_arbitrary(t, &cookie, PA_NATIVE_COOKIE_LENGTH) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    /* Minimum supported version */
    if (c->version < 8) {
        pa_pstream_send_error(c->pstream, tag, PA_ERR_VERSION);
        return;
    }

    /* Starting with protocol version 13 the MSB of the version tag
       reflects if shm is available for this pa_native_connection or
       not. */
    if ((c->version & PA_PROTOCOL_VERSION_MASK) >= 13) {
        shm_on_remote = !!(c->version & PA_PROTOCOL_FLAG_SHM);

        /* Starting with protocol version 31, the second MSB of the version
         * tag reflects whether memfd is supported on the other PA end. */
        if ((c->version & PA_PROTOCOL_VERSION_MASK) >= 31)
            memfd_on_remote = !!(c->version & PA_PROTOCOL_FLAG_MEMFD);

        /* Reserve the two most-significant _bytes_ of the version tag
         * for flags. */
        c->version &= PA_PROTOCOL_VERSION_MASK;
    }

    pa_log_debug("Protocol version: remote %u, local %u", c->version, PA_PROTOCOL_VERSION);

    pa_proplist_setf(c->client->proplist, "native-protocol.version", "%u", c->version);

    if (!c->authorized) {
        bool success = false;

#ifdef HAVE_CREDS
        const pa_creds *creds;

        if ((creds = pa_pdispatch_creds(pd))) {
            if (creds->uid == getuid())
                success = true;
            else if (c->options->auth_group) {
                int r;
                gid_t gid;

                if ((gid = pa_get_gid_of_group(c->options->auth_group)) == (gid_t) -1)
                    pa_log_warn("Failed to get GID of group '%s'", c->options->auth_group);
                else if (gid == creds->gid)
                    success = true;

                if (!success) {
                    if ((r = pa_uid_in_group(creds->uid, c->options->auth_group)) < 0)
                        pa_log_warn("Failed to check group membership.");
                    else if (r > 0)
                        success = true;
                }
            }

            pa_log_info("Got credentials: uid=%lu gid=%lu success=%i",
                        (unsigned long) creds->uid,
                        (unsigned long) creds->gid,
                        (int) success);
        }
#endif

        if (!success && c->options->auth_cookie) {
            const uint8_t *ac;

            if ((ac = pa_auth_cookie_read(c->options->auth_cookie, PA_NATIVE_COOKIE_LENGTH)))
                if (memcmp(ac, cookie, PA_NATIVE_COOKIE_LENGTH) == 0)
                    success = true;
        }

        if (!success) {
            pa_log_warn("Denied access to client with invalid authentication data.");
            pa_pstream_send_error(c->pstream, tag, PA_ERR_ACCESS);
            return;
        }

        c->authorized = true;
        if (c->auth_timeout_event) {
            c->protocol->core->mainloop->time_free(c->auth_timeout_event);
            c->auth_timeout_event = NULL;
        }
    }

    /* Enable shared memory and memfd support if possible */
    do_shm =
        pa_mempool_is_shared(c->protocol->core->mempool) &&
        c->is_local;

    pa_log_debug("SHM possible: %s", pa_yes_no(do_shm));

    if (do_shm)
        if (c->version < 10 || (c->version >= 13 && !shm_on_remote))
            do_shm = false;

#ifdef HAVE_CREDS
    if (do_shm) {
        /* Only enable SHM if both sides are owned by the same
         * user. This is a security measure because otherwise data
         * private to the user might leak. */

        const pa_creds *creds;
        if (!(creds = pa_pdispatch_creds(pd)) || getuid() != creds->uid)
            do_shm = false;
    }
#endif

    pa_log_debug("Negotiated SHM: %s", pa_yes_no(do_shm));
    pa_pstream_enable_shm(c->pstream, do_shm);

    /* Do not declare memfd support for 9.0 client libraries (protocol v31).
     *
     * Although they support memfd transport, such 9.0 clients has an iochannel
     * bug that would break memfd audio if they're run in 32-bit mode over a
     * 64-bit kernel. Thus influence them to use the POSIX shared memory model
     * instead. Check commit 451d1d676237c81 for further details. */
    do_memfd =
        c->version >= 32 && do_shm && pa_mempool_is_memfd_backed(c->protocol->core->mempool);

    shm_type = PA_MEM_TYPE_PRIVATE;
    if (do_shm) {
        if (do_memfd && memfd_on_remote) {
            pa_pstream_enable_memfd(c->pstream);
            shm_type = PA_MEM_TYPE_SHARED_MEMFD;
        } else
            shm_type = PA_MEM_TYPE_SHARED_POSIX;

        pa_log_debug("Memfd possible: %s", pa_yes_no(pa_memfd_is_locally_supported()));
        pa_log_debug("Negotiated SHM type: %s", pa_mem_type_to_string(shm_type));
    }

    reply = reply_new(tag);
    pa_tagstruct_putu32(reply, PA_PROTOCOL_VERSION | (do_shm ? 0x80000000 : 0) |
                        (do_memfd ? 0x40000000 : 0));

#ifdef HAVE_CREDS
{
    /* SHM support is only enabled after both sides made sure they are the same user. */

    pa_creds ucred;

    ucred.uid = getuid();
    ucred.gid = getgid();

    pa_pstream_send_tagstruct_with_creds(c->pstream, reply, &ucred);
}
#else
    pa_pstream_send_tagstruct(c->pstream, reply);
#endif

    /* The client enables memfd transport on its pstream only after
     * inspecting our version flags to see if we support memfds too.
     *
     * Thus register any pools after sending the server's version
     * flags and _never_ before it. */
    if (shm_type == PA_MEM_TYPE_SHARED_MEMFD) {
        const char *reason;

        if (pa_pstream_register_memfd_mempool(c->pstream, c->protocol->core->mempool, &reason))
            pa_log("Failed to register memfd mempool. Reason: %s", reason);
    }

    setup_srbchannel(c, shm_type);
}

static void command_register_memfd_shmid(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_common_command_register_memfd_shmid(c->pstream, pd, c->version, command, t))
        protocol_error(c);
}

static void command_set_client_name(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    const char *name = NULL;
    pa_proplist *p;
    pa_tagstruct *reply;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    p = pa_proplist_new();

    if ((c->version < 13 && pa_tagstruct_gets(t, &name) < 0) ||
        (c->version >= 13 && pa_tagstruct_get_proplist(t, p) < 0) ||
        !pa_tagstruct_eof(t)) {

        protocol_error(c);
        pa_proplist_free(p);
        return;
    }

    if (name)
        if (pa_proplist_sets(p, PA_PROP_APPLICATION_NAME, name) < 0) {
            pa_pstream_send_error(c->pstream, tag, PA_ERR_INVALID);
            pa_proplist_free(p);
            return;
        }

    pa_client_update_proplist(c->client, PA_UPDATE_REPLACE, p);
    pa_proplist_free(p);

    reply = reply_new(tag);

    if (c->version >= 13)
        pa_tagstruct_putu32(reply, c->client->index);

    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_lookup(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    const char *name;
    uint32_t idx = PA_IDXSET_INVALID;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_gets(t, &name) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, name && pa_namereg_is_valid_name_or_wildcard(name, command == PA_COMMAND_LOOKUP_SINK ? PA_NAMEREG_SINK : PA_NAMEREG_SOURCE), tag, PA_ERR_INVALID);

    if (command == PA_COMMAND_LOOKUP_SINK) {
        pa_sink *sink;
        if ((sink = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SINK)))
            idx = sink->index;
    } else {
        pa_source *source;
        pa_assert(command == PA_COMMAND_LOOKUP_SOURCE);
        if ((source = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SOURCE)))
            idx = source->index;
    }

    if (idx == PA_IDXSET_INVALID)
        pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
    else {
        pa_tagstruct *reply;
        reply = reply_new(tag);
        pa_tagstruct_putu32(reply, idx);
        pa_pstream_send_tagstruct(c->pstream, reply);
    }
}

static void command_drain_playback_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    playback_stream *s;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    s = pa_idxset_get_by_index(c->output_streams, idx);
    CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);
    CHECK_VALIDITY(c->pstream, playback_stream_isinstance(s), tag, PA_ERR_NOENTITY);

    pa_asyncmsgq_post(s->sink_input->sink->asyncmsgq, PA_MSGOBJECT(s->sink_input), SINK_INPUT_MESSAGE_DRAIN, PA_UINT_TO_PTR(tag), 0, NULL, NULL);
}

static void command_stat(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    pa_tagstruct *reply;
    const pa_mempool_stat *stat;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (!pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    stat = pa_mempool_get_stat(c->protocol->core->mempool);

    reply = reply_new(tag);
    pa_tagstruct_putu32(reply, (uint32_t) pa_atomic_load(&stat->n_allocated));
    pa_tagstruct_putu32(reply, (uint32_t) pa_atomic_load(&stat->allocated_size));
    pa_tagstruct_putu32(reply, (uint32_t) pa_atomic_load(&stat->n_accumulated));
    pa_tagstruct_putu32(reply, (uint32_t) pa_atomic_load(&stat->accumulated_size));
    pa_tagstruct_putu32(reply, (uint32_t) pa_scache_total_size(c->protocol->core));
    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_get_playback_latency(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    pa_tagstruct *reply;
    playback_stream *s;
    struct timeval tv, now;
    uint32_t idx;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_get_timeval(t, &tv) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    s = pa_idxset_get_by_index(c->output_streams, idx);
    CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);
    CHECK_VALIDITY(c->pstream, playback_stream_isinstance(s), tag, PA_ERR_NOENTITY);

    /* Get an atomic snapshot of all timing parameters */
    pa_assert_se(pa_asyncmsgq_send(s->sink_input->sink->asyncmsgq, PA_MSGOBJECT(s->sink_input), SINK_INPUT_MESSAGE_UPDATE_LATENCY, s, 0, NULL) == 0);

    reply = reply_new(tag);
    pa_tagstruct_put_usec(reply,
                          s->current_sink_latency +
                          pa_bytes_to_usec(s->render_memblockq_length, &s->sink_input->sink->sample_spec));
    pa_tagstruct_put_usec(reply, 0);
    pa_tagstruct_put_boolean(reply,
                             s->playing_for > 0 &&
                             s->sink_input->sink->state == PA_SINK_RUNNING &&
                             s->sink_input->state == PA_SINK_INPUT_RUNNING);
    pa_tagstruct_put_timeval(reply, &tv);
    pa_tagstruct_put_timeval(reply, pa_gettimeofday(&now));
    pa_tagstruct_puts64(reply, s->write_index);
    pa_tagstruct_puts64(reply, s->read_index);

    if (c->version >= 13) {
        pa_tagstruct_putu64(reply, s->underrun_for);
        pa_tagstruct_putu64(reply, s->playing_for);
    }

    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_get_record_latency(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    pa_tagstruct *reply;
    record_stream *s;
    struct timeval tv, now;
    uint32_t idx;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_get_timeval(t, &tv) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    s = pa_idxset_get_by_index(c->record_streams, idx);
    CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);

    /* Get an atomic snapshot of all timing parameters */
    pa_assert_se(pa_asyncmsgq_send(s->source_output->source->asyncmsgq, PA_MSGOBJECT(s->source_output), SOURCE_OUTPUT_MESSAGE_UPDATE_LATENCY, s, 0, NULL) == 0);

    reply = reply_new(tag);
    pa_tagstruct_put_usec(reply, s->current_monitor_latency);
    pa_tagstruct_put_usec(reply,
                          s->current_source_latency +
                          pa_bytes_to_usec(s->on_the_fly_snapshot, &s->source_output->sample_spec));
    pa_tagstruct_put_boolean(reply,
                             s->source_output->source->state == PA_SOURCE_RUNNING &&
                             s->source_output->state == PA_SOURCE_OUTPUT_RUNNING);
    pa_tagstruct_put_timeval(reply, &tv);
    pa_tagstruct_put_timeval(reply, pa_gettimeofday(&now));
    pa_tagstruct_puts64(reply, pa_memblockq_get_write_index(s->memblockq));
    pa_tagstruct_puts64(reply, pa_memblockq_get_read_index(s->memblockq));
    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_create_upload_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    upload_stream *s;
    uint32_t length;
    const char *name = NULL;
    pa_sample_spec ss;
    pa_channel_map map;
    pa_tagstruct *reply;
    pa_proplist *p;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_gets(t, &name) < 0 ||
        pa_tagstruct_get_sample_spec(t, &ss) < 0 ||
        pa_tagstruct_get_channel_map(t, &map) < 0 ||
        pa_tagstruct_getu32(t, &length) < 0) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, pa_sample_spec_valid(&ss), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, pa_channel_map_valid(&map), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, map.channels == ss.channels, tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, (length % pa_frame_size(&ss)) == 0 && length > 0, tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, length <= PA_SCACHE_ENTRY_SIZE_MAX, tag, PA_ERR_TOOLARGE);

    p = pa_proplist_new();

    if ((c->version >= 13 && pa_tagstruct_get_proplist(t, p) < 0) ||
        !pa_tagstruct_eof(t)) {

        protocol_error(c);
        pa_proplist_free(p);
        return;
    }

    if (c->version < 13)
        pa_proplist_sets(p, PA_PROP_MEDIA_NAME, name);
    else if (!name)
        if (!(name = pa_proplist_gets(p, PA_PROP_EVENT_ID)))
            name = pa_proplist_gets(p, PA_PROP_MEDIA_NAME);

    if (!name || !pa_namereg_is_valid_name(name)) {
        pa_proplist_free(p);
        CHECK_VALIDITY(c->pstream, false, tag, PA_ERR_INVALID);
    }

    s = upload_stream_new(c, &ss, &map, name, length, p);
    pa_proplist_free(p);

    CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_INVALID);

    reply = reply_new(tag);
    pa_tagstruct_putu32(reply, s->index);
    pa_tagstruct_putu32(reply, length);
    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_finish_upload_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t channel;
    upload_stream *s;
    uint32_t idx;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &channel) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    s = pa_idxset_get_by_index(c->output_streams, channel);
    CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);
    CHECK_VALIDITY(c->pstream, upload_stream_isinstance(s), tag, PA_ERR_NOENTITY);

    if (!s->memchunk.memblock)
        pa_pstream_send_error(c->pstream, tag, PA_ERR_TOOLARGE);
    else if (pa_scache_add_item(c->protocol->core, s->name, &s->sample_spec, &s->channel_map, &s->memchunk, s->proplist, &idx) < 0)
        pa_pstream_send_error(c->pstream, tag, PA_ERR_INTERNAL);
    else
        pa_pstream_send_simple_ack(c->pstream, tag);

    upload_stream_unlink(s);
}

static void command_play_sample(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t sink_index;
    pa_volume_t volume;
    pa_sink *sink;
    const char *name, *sink_name;
    uint32_t idx;
    pa_proplist *p;
    pa_tagstruct *reply;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    if (pa_tagstruct_getu32(t, &sink_index) < 0 ||
        pa_tagstruct_gets(t, &sink_name) < 0 ||
        pa_tagstruct_getu32(t, &volume) < 0 ||
        pa_tagstruct_gets(t, &name) < 0) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, !sink_name || pa_namereg_is_valid_name_or_wildcard(sink_name, PA_NAMEREG_SINK), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, sink_index == PA_INVALID_INDEX || !sink_name, tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, !sink_name || sink_index == PA_INVALID_INDEX, tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, name && pa_namereg_is_valid_name(name), tag, PA_ERR_INVALID);

    if (sink_index != PA_INVALID_INDEX)
        sink = pa_idxset_get_by_index(c->protocol->core->sinks, sink_index);
    else
        sink = pa_namereg_get(c->protocol->core, sink_name, PA_NAMEREG_SINK);

    CHECK_VALIDITY(c->pstream, sink, tag, PA_ERR_NOENTITY);

    p = pa_proplist_new();

    if ((c->version >= 13 && pa_tagstruct_get_proplist(t, p) < 0) ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        pa_proplist_free(p);
        return;
    }

    pa_proplist_update(p, PA_UPDATE_MERGE, c->client->proplist);

    if (pa_scache_play_item(c->protocol->core, name, sink, volume, p, &idx) < 0) {
        pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
        pa_proplist_free(p);
        return;
    }

    pa_proplist_free(p);

    reply = reply_new(tag);

    if (c->version >= 13)
        pa_tagstruct_putu32(reply, idx);

    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_remove_sample(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    const char *name;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_gets(t, &name) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, name && pa_namereg_is_valid_name(name), tag, PA_ERR_INVALID);

    if (pa_scache_remove_item(c->protocol->core, name) < 0) {
        pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
        return;
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void fixup_sample_spec(pa_native_connection *c, pa_sample_spec *fixed, const pa_sample_spec *original) {
    pa_assert(c);
    pa_assert(fixed);
    pa_assert(original);

    *fixed = *original;

    if (c->version < 12) {
        /* Before protocol version 12 we didn't support S32 samples,
         * so we need to lie about this to the client */

        if (fixed->format == PA_SAMPLE_S32LE)
            fixed->format = PA_SAMPLE_FLOAT32LE;
        if (fixed->format == PA_SAMPLE_S32BE)
            fixed->format = PA_SAMPLE_FLOAT32BE;
    }

    if (c->version < 15) {
        if (fixed->format == PA_SAMPLE_S24LE || fixed->format == PA_SAMPLE_S24_32LE)
            fixed->format = PA_SAMPLE_FLOAT32LE;
        if (fixed->format == PA_SAMPLE_S24BE || fixed->format == PA_SAMPLE_S24_32BE)
            fixed->format = PA_SAMPLE_FLOAT32BE;
    }
}

static void sink_fill_tagstruct(pa_native_connection *c, pa_tagstruct *t, pa_sink *sink) {
    pa_sample_spec fixed_ss;

    pa_assert(t);
    pa_sink_assert_ref(sink);

    fixup_sample_spec(c, &fixed_ss, &sink->sample_spec);

    pa_tagstruct_put(
        t,
        PA_TAG_U32, sink->index,
        PA_TAG_STRING, sink->name,
        PA_TAG_STRING, pa_strnull(pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_DESCRIPTION)),
        PA_TAG_SAMPLE_SPEC, &fixed_ss,
        PA_TAG_CHANNEL_MAP, &sink->channel_map,
        PA_TAG_U32, sink->module ? sink->module->index : PA_INVALID_INDEX,
        PA_TAG_CVOLUME, pa_sink_get_volume(sink, false),
        PA_TAG_BOOLEAN, pa_sink_get_mute(sink, false),
        PA_TAG_U32, sink->monitor_source ? sink->monitor_source->index : PA_INVALID_INDEX,
        PA_TAG_STRING, sink->monitor_source ? sink->monitor_source->name : NULL,
        PA_TAG_USEC, pa_sink_get_latency(sink),
        PA_TAG_STRING, sink->driver,
        PA_TAG_U32, sink->flags & PA_SINK_CLIENT_FLAGS_MASK,
        PA_TAG_INVALID);

    if (c->version >= 13) {
        pa_tagstruct_put_proplist(t, sink->proplist);
        pa_tagstruct_put_usec(t, pa_sink_get_requested_latency(sink));
    }

    if (c->version >= 15) {
        pa_tagstruct_put_volume(t, sink->base_volume);
        if (PA_UNLIKELY(sink->state == PA_SINK_INVALID_STATE))
            pa_log_error("Internal sink state is invalid.");
        pa_tagstruct_putu32(t, sink->state);
        pa_tagstruct_putu32(t, sink->n_volume_steps);
        pa_tagstruct_putu32(t, sink->card ? sink->card->index : PA_INVALID_INDEX);
    }

    if (c->version >= 16) {
        void *state;
        pa_device_port *p;

        pa_tagstruct_putu32(t, pa_hashmap_size(sink->ports));

        PA_HASHMAP_FOREACH(p, sink->ports, state) {
            pa_tagstruct_puts(t, p->name);
            pa_tagstruct_puts(t, p->description);
            pa_tagstruct_putu32(t, p->priority);
            if (c->version >= 24) {
                pa_tagstruct_putu32(t, p->available);
                if (c->version >= 34) {
                    pa_tagstruct_puts(t, p->availability_group);
                    pa_tagstruct_putu32(t, p->type);
                }
            }
        }

        pa_tagstruct_puts(t, sink->active_port ? sink->active_port->name : NULL);
    }

    if (c->version >= 21) {
        uint32_t i;
        pa_format_info *f;
        pa_idxset *formats = pa_sink_get_formats(sink);

        pa_tagstruct_putu8(t, (uint8_t) pa_idxset_size(formats));
        PA_IDXSET_FOREACH(f, formats, i) {
            pa_tagstruct_put_format_info(t, f);
        }

        pa_idxset_free(formats, (pa_free_cb_t) pa_format_info_free);
    }
}

static void source_fill_tagstruct(pa_native_connection *c, pa_tagstruct *t, pa_source *source) {
    pa_sample_spec fixed_ss;

    pa_assert(t);
    pa_source_assert_ref(source);

    fixup_sample_spec(c, &fixed_ss, &source->sample_spec);

    pa_tagstruct_put(
        t,
        PA_TAG_U32, source->index,
        PA_TAG_STRING, source->name,
        PA_TAG_STRING, pa_strnull(pa_proplist_gets(source->proplist, PA_PROP_DEVICE_DESCRIPTION)),
        PA_TAG_SAMPLE_SPEC, &fixed_ss,
        PA_TAG_CHANNEL_MAP, &source->channel_map,
        PA_TAG_U32, source->module ? source->module->index : PA_INVALID_INDEX,
        PA_TAG_CVOLUME, pa_source_get_volume(source, false),
        PA_TAG_BOOLEAN, pa_source_get_mute(source, false),
        PA_TAG_U32, source->monitor_of ? source->monitor_of->index : PA_INVALID_INDEX,
        PA_TAG_STRING, source->monitor_of ? source->monitor_of->name : NULL,
        PA_TAG_USEC, pa_source_get_latency(source),
        PA_TAG_STRING, source->driver,
        PA_TAG_U32, source->flags & PA_SOURCE_CLIENT_FLAGS_MASK,
        PA_TAG_INVALID);

    if (c->version >= 13) {
        pa_tagstruct_put_proplist(t, source->proplist);
        pa_tagstruct_put_usec(t, pa_source_get_requested_latency(source));
    }

    if (c->version >= 15) {
        pa_tagstruct_put_volume(t, source->base_volume);
        if (PA_UNLIKELY(source->state == PA_SOURCE_INVALID_STATE))
            pa_log_error("Internal source state is invalid.");
        pa_tagstruct_putu32(t, source->state);
        pa_tagstruct_putu32(t, source->n_volume_steps);
        pa_tagstruct_putu32(t, source->card ? source->card->index : PA_INVALID_INDEX);
    }

    if (c->version >= 16) {
        void *state;
        pa_device_port *p;

        pa_tagstruct_putu32(t, pa_hashmap_size(source->ports));

        PA_HASHMAP_FOREACH(p, source->ports, state) {
            pa_tagstruct_puts(t, p->name);
            pa_tagstruct_puts(t, p->description);
            pa_tagstruct_putu32(t, p->priority);
            if (c->version >= 24) {
                pa_tagstruct_putu32(t, p->available);
                if (c->version >= 34) {
                    pa_tagstruct_puts(t, p->availability_group);
                    pa_tagstruct_putu32(t, p->type);
                }
            }
        }

        pa_tagstruct_puts(t, source->active_port ? source->active_port->name : NULL);
    }

    if (c->version >= 22) {
        uint32_t i;
        pa_format_info *f;
        pa_idxset *formats = pa_source_get_formats(source);

        pa_tagstruct_putu8(t, (uint8_t) pa_idxset_size(formats));
        PA_IDXSET_FOREACH(f, formats, i) {
            pa_tagstruct_put_format_info(t, f);
        }

        pa_idxset_free(formats, (pa_free_cb_t) pa_format_info_free);
    }
}

static void client_fill_tagstruct(pa_native_connection *c, pa_tagstruct *t, pa_client *client) {
    pa_assert(t);
    pa_assert(client);

    pa_tagstruct_putu32(t, client->index);
    pa_tagstruct_puts(t, pa_strnull(pa_proplist_gets(client->proplist, PA_PROP_APPLICATION_NAME)));
    pa_tagstruct_putu32(t, client->module ? client->module->index : PA_INVALID_INDEX);
    pa_tagstruct_puts(t, client->driver);

    if (c->version >= 13)
        pa_tagstruct_put_proplist(t, client->proplist);
}

static void card_fill_tagstruct(pa_native_connection *c, pa_tagstruct *t, pa_card *card) {
    void *state = NULL;
    pa_card_profile *p;
    pa_device_port *port;

    pa_assert(t);
    pa_assert(card);

    pa_tagstruct_putu32(t, card->index);
    pa_tagstruct_puts(t, card->name);
    pa_tagstruct_putu32(t, card->module ? card->module->index : PA_INVALID_INDEX);
    pa_tagstruct_puts(t, card->driver);

    pa_tagstruct_putu32(t, pa_hashmap_size(card->profiles));

    PA_HASHMAP_FOREACH(p, card->profiles, state) {
        pa_tagstruct_puts(t, p->name);
        pa_tagstruct_puts(t, p->description);
        pa_tagstruct_putu32(t, p->n_sinks);
        pa_tagstruct_putu32(t, p->n_sources);
        pa_tagstruct_putu32(t, p->priority);

        if (c->version >= 29)
            pa_tagstruct_putu32(t, (p->available != PA_AVAILABLE_NO));
    }

    pa_tagstruct_puts(t, card->active_profile->name);
    pa_tagstruct_put_proplist(t, card->proplist);

    if (c->version < 26)
        return;

    pa_tagstruct_putu32(t, pa_hashmap_size(card->ports));

    PA_HASHMAP_FOREACH(port, card->ports, state) {
        void *state2;

        pa_tagstruct_puts(t, port->name);
        pa_tagstruct_puts(t, port->description);
        pa_tagstruct_putu32(t, port->priority);
        pa_tagstruct_putu32(t, port->available);
        pa_tagstruct_putu8(t, port->direction);
        pa_tagstruct_put_proplist(t, port->proplist);

        pa_tagstruct_putu32(t, pa_hashmap_size(port->profiles));

        PA_HASHMAP_FOREACH(p, port->profiles, state2)
            pa_tagstruct_puts(t, p->name);

        if (c->version >= 27) {
            pa_tagstruct_puts64(t, port->latency_offset);
            if (c->version >= 34) {
                pa_tagstruct_puts(t, port->availability_group);
                pa_tagstruct_putu32(t, port->type);
            }
        }
    }
}

static void module_fill_tagstruct(pa_native_connection *c, pa_tagstruct *t, pa_module *module) {
    pa_assert(t);
    pa_assert(module);

    pa_tagstruct_putu32(t, module->index);
    pa_tagstruct_puts(t, module->name);
    pa_tagstruct_puts(t, module->argument);
    pa_tagstruct_putu32(t, (uint32_t) pa_module_get_n_used(module));

    if (c->version < 15)
        pa_tagstruct_put_boolean(t, false); /* autoload is obsolete */

    if (c->version >= 15)
        pa_tagstruct_put_proplist(t, module->proplist);
}

static void sink_input_fill_tagstruct(pa_native_connection *c, pa_tagstruct *t, pa_sink_input *s) {
    pa_sample_spec fixed_ss;
    pa_usec_t sink_latency;
    pa_cvolume v;
    bool has_volume = false;

    pa_assert(t);
    pa_sink_input_assert_ref(s);

    fixup_sample_spec(c, &fixed_ss, &s->sample_spec);

    has_volume = pa_sink_input_is_volume_readable(s);
    if (has_volume)
        pa_sink_input_get_volume(s, &v, true);
    else
        pa_cvolume_reset(&v, fixed_ss.channels);

    pa_tagstruct_putu32(t, s->index);
    pa_tagstruct_puts(t, pa_strnull(pa_proplist_gets(s->proplist, PA_PROP_MEDIA_NAME)));
    pa_tagstruct_putu32(t, s->module ? s->module->index : PA_INVALID_INDEX);
    pa_tagstruct_putu32(t, s->client ? s->client->index : PA_INVALID_INDEX);
    pa_tagstruct_putu32(t, s->sink->index);
    pa_tagstruct_put_sample_spec(t, &fixed_ss);
    pa_tagstruct_put_channel_map(t, &s->channel_map);
    pa_tagstruct_put_cvolume(t, &v);
    pa_tagstruct_put_usec(t, pa_sink_input_get_latency(s, &sink_latency));
    pa_tagstruct_put_usec(t, sink_latency);
    pa_tagstruct_puts(t, pa_resample_method_to_string(pa_sink_input_get_resample_method(s)));
    pa_tagstruct_puts(t, s->driver);
    if (c->version >= 11)
        pa_tagstruct_put_boolean(t, s->muted);
    if (c->version >= 13)
        pa_tagstruct_put_proplist(t, s->proplist);
    if (c->version >= 19)
        pa_tagstruct_put_boolean(t, s->state == PA_SINK_INPUT_CORKED);
    if (c->version >= 20) {
        pa_tagstruct_put_boolean(t, has_volume);
        pa_tagstruct_put_boolean(t, s->volume_writable);
    }
    if (c->version >= 21)
        pa_tagstruct_put_format_info(t, s->format);
}

static void source_output_fill_tagstruct(pa_native_connection *c, pa_tagstruct *t, pa_source_output *s) {
    pa_sample_spec fixed_ss;
    pa_usec_t source_latency;
    pa_cvolume v;
    bool has_volume = false;

    pa_assert(t);
    pa_source_output_assert_ref(s);

    fixup_sample_spec(c, &fixed_ss, &s->sample_spec);

    has_volume = pa_source_output_is_volume_readable(s);
    if (has_volume)
        pa_source_output_get_volume(s, &v, true);
    else
        pa_cvolume_reset(&v, fixed_ss.channels);

    pa_tagstruct_putu32(t, s->index);
    pa_tagstruct_puts(t, pa_strnull(pa_proplist_gets(s->proplist, PA_PROP_MEDIA_NAME)));
    pa_tagstruct_putu32(t, s->module ? s->module->index : PA_INVALID_INDEX);
    pa_tagstruct_putu32(t, s->client ? s->client->index : PA_INVALID_INDEX);
    pa_tagstruct_putu32(t, s->source->index);
    pa_tagstruct_put_sample_spec(t, &fixed_ss);
    pa_tagstruct_put_channel_map(t, &s->channel_map);
    pa_tagstruct_put_usec(t, pa_source_output_get_latency(s, &source_latency));
    pa_tagstruct_put_usec(t, source_latency);
    pa_tagstruct_puts(t, pa_resample_method_to_string(pa_source_output_get_resample_method(s)));
    pa_tagstruct_puts(t, s->driver);
    if (c->version >= 13)
        pa_tagstruct_put_proplist(t, s->proplist);
    if (c->version >= 19)
        pa_tagstruct_put_boolean(t, s->state == PA_SOURCE_OUTPUT_CORKED);
    if (c->version >= 22) {
        pa_tagstruct_put_cvolume(t, &v);
        pa_tagstruct_put_boolean(t, s->muted);
        pa_tagstruct_put_boolean(t, has_volume);
        pa_tagstruct_put_boolean(t, s->volume_writable);
        pa_tagstruct_put_format_info(t, s->format);
    }
}

static void scache_fill_tagstruct(pa_native_connection *c, pa_tagstruct *t, pa_scache_entry *e) {
    pa_sample_spec fixed_ss;
    pa_cvolume v;

    pa_assert(t);
    pa_assert(e);

    if (e->memchunk.memblock)
        fixup_sample_spec(c, &fixed_ss, &e->sample_spec);
    else
        memset(&fixed_ss, 0, sizeof(fixed_ss));

    pa_tagstruct_putu32(t, e->index);
    pa_tagstruct_puts(t, e->name);

    if (e->volume_is_set)
        v = e->volume;
    else
        pa_cvolume_init(&v);

    pa_tagstruct_put_cvolume(t, &v);
    pa_tagstruct_put_usec(t, e->memchunk.memblock ? pa_bytes_to_usec(e->memchunk.length, &e->sample_spec) : 0);
    pa_tagstruct_put_sample_spec(t, &fixed_ss);
    pa_tagstruct_put_channel_map(t, &e->channel_map);
    pa_tagstruct_putu32(t, (uint32_t) e->memchunk.length);
    pa_tagstruct_put_boolean(t, e->lazy);
    pa_tagstruct_puts(t, e->filename);

    if (c->version >= 13)
        pa_tagstruct_put_proplist(t, e->proplist);
}

static void command_get_info(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    pa_sink *sink = NULL;
    pa_source *source = NULL;
    pa_client *client = NULL;
    pa_card *card = NULL;
    pa_module *module = NULL;
    pa_sink_input *si = NULL;
    pa_source_output *so = NULL;
    pa_scache_entry *sce = NULL;
    const char *name = NULL;
    pa_tagstruct *reply;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        (command != PA_COMMAND_GET_CLIENT_INFO &&
         command != PA_COMMAND_GET_MODULE_INFO &&
         command != PA_COMMAND_GET_SINK_INPUT_INFO &&
         command != PA_COMMAND_GET_SOURCE_OUTPUT_INFO &&
         pa_tagstruct_gets(t, &name) < 0) ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, !name ||
                   (command == PA_COMMAND_GET_SINK_INFO &&
                    pa_namereg_is_valid_name_or_wildcard(name, PA_NAMEREG_SINK)) ||
                   (command == PA_COMMAND_GET_SOURCE_INFO &&
                    pa_namereg_is_valid_name_or_wildcard(name, PA_NAMEREG_SOURCE)) ||
                   pa_namereg_is_valid_name(name), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, command == PA_COMMAND_GET_SINK_INFO ||
                   command == PA_COMMAND_GET_SOURCE_INFO ||
                   (idx != PA_INVALID_INDEX || name), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, idx == PA_INVALID_INDEX || !name, tag, PA_ERR_INVALID);

    if (command == PA_COMMAND_GET_SINK_INFO) {
        if (idx != PA_INVALID_INDEX)
            sink = pa_idxset_get_by_index(c->protocol->core->sinks, idx);
        else
            sink = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SINK);
    } else if (command == PA_COMMAND_GET_SOURCE_INFO) {
        if (idx != PA_INVALID_INDEX)
            source = pa_idxset_get_by_index(c->protocol->core->sources, idx);
        else
            source = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SOURCE);
    } else if (command == PA_COMMAND_GET_CARD_INFO) {
        if (idx != PA_INVALID_INDEX)
            card = pa_idxset_get_by_index(c->protocol->core->cards, idx);
        else
            card = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_CARD);
    } else if (command == PA_COMMAND_GET_CLIENT_INFO)
        client = pa_idxset_get_by_index(c->protocol->core->clients, idx);
    else if (command == PA_COMMAND_GET_MODULE_INFO)
        module = pa_idxset_get_by_index(c->protocol->core->modules, idx);
    else if (command == PA_COMMAND_GET_SINK_INPUT_INFO)
        si = pa_idxset_get_by_index(c->protocol->core->sink_inputs, idx);
    else if (command == PA_COMMAND_GET_SOURCE_OUTPUT_INFO)
        so = pa_idxset_get_by_index(c->protocol->core->source_outputs, idx);
    else {
        pa_assert(command == PA_COMMAND_GET_SAMPLE_INFO);
        if (idx != PA_INVALID_INDEX)
            sce = pa_idxset_get_by_index(c->protocol->core->scache, idx);
        else
            sce = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SAMPLE);
    }

    if (!sink && !source && !client && !card && !module && !si && !so && !sce) {
        pa_pstream_send_error(c->pstream, tag, PA_ERR_NOENTITY);
        return;
    }

    reply = reply_new(tag);
    if (sink)
        sink_fill_tagstruct(c, reply, sink);
    else if (source)
        source_fill_tagstruct(c, reply, source);
    else if (client)
        client_fill_tagstruct(c, reply, client);
    else if (card)
        card_fill_tagstruct(c, reply, card);
    else if (module)
        module_fill_tagstruct(c, reply, module);
    else if (si)
        sink_input_fill_tagstruct(c, reply, si);
    else if (so)
        source_output_fill_tagstruct(c, reply, so);
    else
        scache_fill_tagstruct(c, reply, sce);
    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_get_info_list(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    pa_idxset *i;
    uint32_t idx;
    void *p;
    pa_tagstruct *reply;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (!pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    reply = reply_new(tag);

    if (command == PA_COMMAND_GET_SINK_INFO_LIST)
        i = c->protocol->core->sinks;
    else if (command == PA_COMMAND_GET_SOURCE_INFO_LIST)
        i = c->protocol->core->sources;
    else if (command == PA_COMMAND_GET_CLIENT_INFO_LIST)
        i = c->protocol->core->clients;
    else if (command == PA_COMMAND_GET_CARD_INFO_LIST)
        i = c->protocol->core->cards;
    else if (command == PA_COMMAND_GET_MODULE_INFO_LIST)
        i = c->protocol->core->modules;
    else if (command == PA_COMMAND_GET_SINK_INPUT_INFO_LIST)
        i = c->protocol->core->sink_inputs;
    else if (command == PA_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST)
        i = c->protocol->core->source_outputs;
    else {
        pa_assert(command == PA_COMMAND_GET_SAMPLE_INFO_LIST);
        i = c->protocol->core->scache;
    }

    if (i) {
        PA_IDXSET_FOREACH(p, i, idx) {
            if (command == PA_COMMAND_GET_SINK_INFO_LIST)
                sink_fill_tagstruct(c, reply, p);
            else if (command == PA_COMMAND_GET_SOURCE_INFO_LIST)
                source_fill_tagstruct(c, reply, p);
            else if (command == PA_COMMAND_GET_CLIENT_INFO_LIST)
                client_fill_tagstruct(c, reply, p);
            else if (command == PA_COMMAND_GET_CARD_INFO_LIST)
                card_fill_tagstruct(c, reply, p);
            else if (command == PA_COMMAND_GET_MODULE_INFO_LIST)
                module_fill_tagstruct(c, reply, p);
            else if (command == PA_COMMAND_GET_SINK_INPUT_INFO_LIST)
                sink_input_fill_tagstruct(c, reply, p);
            else if (command == PA_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST)
                source_output_fill_tagstruct(c, reply, p);
            else {
                pa_assert(command == PA_COMMAND_GET_SAMPLE_INFO_LIST);
                scache_fill_tagstruct(c, reply, p);
            }
        }
    }

    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_get_server_info(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    pa_tagstruct *reply;
    pa_sample_spec fixed_ss;
    char *h, *u;
    pa_core *core;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (!pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    reply = reply_new(tag);
    pa_tagstruct_puts(reply, PACKAGE_NAME);
    pa_tagstruct_puts(reply, PACKAGE_VERSION);

    u = pa_get_user_name_malloc();
    pa_tagstruct_puts(reply, u);
    pa_xfree(u);

    h = pa_get_host_name_malloc();
    pa_tagstruct_puts(reply, h);
    pa_xfree(h);

    core = c->protocol->core;

    fixup_sample_spec(c, &fixed_ss, &core->default_sample_spec);
    pa_tagstruct_put_sample_spec(reply, &fixed_ss);

    pa_tagstruct_puts(reply, core->default_sink ? core->default_sink->name : NULL);
    pa_tagstruct_puts(reply, core->default_source ? core->default_source->name : NULL);

    pa_tagstruct_putu32(reply, c->protocol->core->cookie);

    if (c->version >= 15)
        pa_tagstruct_put_channel_map(reply, &core->default_channel_map);

    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void subscription_cb(pa_core *core, pa_subscription_event_type_t e, uint32_t idx, void *userdata) {
    pa_tagstruct *t;
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);

    pa_native_connection_assert_ref(c);

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_SUBSCRIBE_EVENT);
    pa_tagstruct_putu32(t, (uint32_t) -1);
    pa_tagstruct_putu32(t, e);
    pa_tagstruct_putu32(t, idx);
    pa_pstream_send_tagstruct(c->pstream, t);
}

static void command_subscribe(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    pa_subscription_mask_t m;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &m) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, (m & ~PA_SUBSCRIPTION_MASK_ALL) == 0, tag, PA_ERR_INVALID);

    if (c->subscription)
        pa_subscription_free(c->subscription);

    if (m != 0) {
        c->subscription = pa_subscription_new(c->protocol->core, m, subscription_cb, c);
        pa_assert(c->subscription);
    } else
        c->subscription = NULL;

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_set_volume(
        pa_pdispatch *pd,
        uint32_t command,
        uint32_t tag,
        pa_tagstruct *t,
        void *userdata) {

    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    pa_cvolume volume;
    pa_sink *sink = NULL;
    pa_source *source = NULL;
    pa_sink_input *si = NULL;
    pa_source_output *so = NULL;
    const char *name = NULL;
    const char *client_name;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        (command == PA_COMMAND_SET_SINK_VOLUME && pa_tagstruct_gets(t, &name) < 0) ||
        (command == PA_COMMAND_SET_SOURCE_VOLUME && pa_tagstruct_gets(t, &name) < 0) ||
        pa_tagstruct_get_cvolume(t, &volume) ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, !name || pa_namereg_is_valid_name_or_wildcard(name, command == PA_COMMAND_SET_SINK_VOLUME ? PA_NAMEREG_SINK : PA_NAMEREG_SOURCE), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, (idx != PA_INVALID_INDEX) ^ (name != NULL), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, pa_cvolume_valid(&volume), tag, PA_ERR_INVALID);

    switch (command) {

        case PA_COMMAND_SET_SINK_VOLUME:
            if (idx != PA_INVALID_INDEX)
                sink = pa_idxset_get_by_index(c->protocol->core->sinks, idx);
            else
                sink = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SINK);
            break;

        case PA_COMMAND_SET_SOURCE_VOLUME:
            if (idx != PA_INVALID_INDEX)
                source = pa_idxset_get_by_index(c->protocol->core->sources, idx);
            else
                source = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SOURCE);
            break;

        case PA_COMMAND_SET_SINK_INPUT_VOLUME:
            si = pa_idxset_get_by_index(c->protocol->core->sink_inputs, idx);
            break;

        case PA_COMMAND_SET_SOURCE_OUTPUT_VOLUME:
            so = pa_idxset_get_by_index(c->protocol->core->source_outputs, idx);
            break;

        default:
            pa_assert_not_reached();
    }

    CHECK_VALIDITY(c->pstream, si || so || sink || source, tag, PA_ERR_NOENTITY);

    client_name = pa_strnull(pa_proplist_gets(c->client->proplist, PA_PROP_APPLICATION_PROCESS_BINARY));

    if (sink) {
        CHECK_VALIDITY(c->pstream, volume.channels == 1 || pa_cvolume_compatible(&volume, &sink->sample_spec), tag, PA_ERR_INVALID);

        pa_log_debug("Client %s changes volume of sink %s.", client_name, sink->name);
        pa_sink_set_volume(sink, &volume, true, true);
    } else if (source) {
        CHECK_VALIDITY(c->pstream, volume.channels == 1 || pa_cvolume_compatible(&volume, &source->sample_spec), tag, PA_ERR_INVALID);

        pa_log_debug("Client %s changes volume of source %s.", client_name, source->name);
        pa_source_set_volume(source, &volume, true, true);
    } else if (si) {
        CHECK_VALIDITY(c->pstream, si->volume_writable, tag, PA_ERR_BADSTATE);
        CHECK_VALIDITY(c->pstream, volume.channels == 1 || pa_cvolume_compatible(&volume, &si->sample_spec), tag, PA_ERR_INVALID);

        pa_log_debug("Client %s changes volume of sink input %s.",
                     client_name,
                     pa_strnull(pa_proplist_gets(si->proplist, PA_PROP_MEDIA_NAME)));
        pa_sink_input_set_volume(si, &volume, true, true);
    } else if (so) {
        CHECK_VALIDITY(c->pstream, so->volume_writable, tag, PA_ERR_BADSTATE);
        CHECK_VALIDITY(c->pstream, volume.channels == 1 || pa_cvolume_compatible(&volume, &so->sample_spec), tag, PA_ERR_INVALID);

        pa_log_debug("Client %s changes volume of source output %s.",
                     client_name,
                     pa_strnull(pa_proplist_gets(so->proplist, PA_PROP_MEDIA_NAME)));
        pa_source_output_set_volume(so, &volume, true, true);
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_set_mute(
        pa_pdispatch *pd,
        uint32_t command,
        uint32_t tag,
        pa_tagstruct *t,
        void *userdata) {

    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    bool mute;
    pa_sink *sink = NULL;
    pa_source *source = NULL;
    pa_sink_input *si = NULL;
    pa_source_output *so = NULL;
    const char *name = NULL, *client_name;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        (command == PA_COMMAND_SET_SINK_MUTE && pa_tagstruct_gets(t, &name) < 0) ||
        (command == PA_COMMAND_SET_SOURCE_MUTE && pa_tagstruct_gets(t, &name) < 0) ||
        pa_tagstruct_get_boolean(t, &mute) ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, !name || pa_namereg_is_valid_name_or_wildcard(name, command == PA_COMMAND_SET_SINK_MUTE ? PA_NAMEREG_SINK : PA_NAMEREG_SOURCE), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, (idx != PA_INVALID_INDEX) ^ (name != NULL), tag, PA_ERR_INVALID);

    switch (command) {

        case PA_COMMAND_SET_SINK_MUTE:
            if (idx != PA_INVALID_INDEX)
                sink = pa_idxset_get_by_index(c->protocol->core->sinks, idx);
            else
                sink = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SINK);

            break;

        case PA_COMMAND_SET_SOURCE_MUTE:
            if (idx != PA_INVALID_INDEX)
                source = pa_idxset_get_by_index(c->protocol->core->sources, idx);
            else
                source = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SOURCE);

            break;

        case PA_COMMAND_SET_SINK_INPUT_MUTE:
            si = pa_idxset_get_by_index(c->protocol->core->sink_inputs, idx);
            break;

        case PA_COMMAND_SET_SOURCE_OUTPUT_MUTE:
            so = pa_idxset_get_by_index(c->protocol->core->source_outputs, idx);
            break;

        default:
            pa_assert_not_reached();
    }

    CHECK_VALIDITY(c->pstream, si || so || sink || source, tag, PA_ERR_NOENTITY);

    client_name = pa_strnull(pa_proplist_gets(c->client->proplist, PA_PROP_APPLICATION_PROCESS_BINARY));

    if (sink) {
        pa_log_debug("Client %s changes mute of sink %s.", client_name, sink->name);
        pa_sink_set_mute(sink, mute, true);
    } else if (source) {
        pa_log_debug("Client %s changes mute of source %s.", client_name, source->name);
        pa_source_set_mute(source, mute, true);
    } else if (si) {
        pa_log_debug("Client %s changes mute of sink input %s.",
                     client_name,
                     pa_strnull(pa_proplist_gets(si->proplist, PA_PROP_MEDIA_NAME)));
        pa_sink_input_set_mute(si, mute, true);
    } else if (so) {
        pa_log_debug("Client %s changes mute of source output %s.",
                     client_name,
                     pa_strnull(pa_proplist_gets(so->proplist, PA_PROP_MEDIA_NAME)));
        pa_source_output_set_mute(so, mute, true);
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_cork_playback_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    bool b;
    playback_stream *s;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_get_boolean(t, &b) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, idx != PA_INVALID_INDEX, tag, PA_ERR_INVALID);
    s = pa_idxset_get_by_index(c->output_streams, idx);
    CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);
    CHECK_VALIDITY(c->pstream, playback_stream_isinstance(s), tag, PA_ERR_NOENTITY);

    pa_sink_input_cork(s->sink_input, b);

    if (b)
        s->is_underrun = true;

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_trigger_or_flush_or_prebuf_playback_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    playback_stream *s;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, idx != PA_INVALID_INDEX, tag, PA_ERR_INVALID);
    s = pa_idxset_get_by_index(c->output_streams, idx);
    CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);
    CHECK_VALIDITY(c->pstream, playback_stream_isinstance(s), tag, PA_ERR_NOENTITY);

    switch (command) {
        case PA_COMMAND_FLUSH_PLAYBACK_STREAM:
            pa_asyncmsgq_send(s->sink_input->sink->asyncmsgq, PA_MSGOBJECT(s->sink_input), SINK_INPUT_MESSAGE_FLUSH, NULL, 0, NULL);
            break;

        case PA_COMMAND_PREBUF_PLAYBACK_STREAM:
            pa_asyncmsgq_send(s->sink_input->sink->asyncmsgq, PA_MSGOBJECT(s->sink_input), SINK_INPUT_MESSAGE_PREBUF_FORCE, NULL, 0, NULL);
            break;

        case PA_COMMAND_TRIGGER_PLAYBACK_STREAM:
            pa_asyncmsgq_send(s->sink_input->sink->asyncmsgq, PA_MSGOBJECT(s->sink_input), SINK_INPUT_MESSAGE_TRIGGER, NULL, 0, NULL);
            break;

        default:
            pa_assert_not_reached();
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_cork_record_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    record_stream *s;
    bool b;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_get_boolean(t, &b) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    s = pa_idxset_get_by_index(c->record_streams, idx);
    CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);

    pa_source_output_cork(s->source_output, b);
    pa_memblockq_prebuf_force(s->memblockq);
    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_flush_record_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    record_stream *s;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    s = pa_idxset_get_by_index(c->record_streams, idx);
    CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);

    pa_memblockq_flush_read(s->memblockq);
    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_set_stream_buffer_attr(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    pa_buffer_attr a;
    pa_tagstruct *reply;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    memset(&a, 0, sizeof(a));

    if (pa_tagstruct_getu32(t, &idx) < 0) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    if (command == PA_COMMAND_SET_PLAYBACK_STREAM_BUFFER_ATTR) {
        playback_stream *s;
        bool adjust_latency = false, early_requests = false;

        s = pa_idxset_get_by_index(c->output_streams, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);
        CHECK_VALIDITY(c->pstream, playback_stream_isinstance(s), tag, PA_ERR_NOENTITY);

        if (pa_tagstruct_get(
                    t,
                    PA_TAG_U32, &a.maxlength,
                    PA_TAG_U32, &a.tlength,
                    PA_TAG_U32, &a.prebuf,
                    PA_TAG_U32, &a.minreq,
                    PA_TAG_INVALID) < 0 ||
            (c->version >= 13 && pa_tagstruct_get_boolean(t, &adjust_latency) < 0) ||
            (c->version >= 14 && pa_tagstruct_get_boolean(t, &early_requests) < 0) ||
            !pa_tagstruct_eof(t)) {
            protocol_error(c);
            return;
        }

        s->adjust_latency = adjust_latency;
        s->early_requests = early_requests;
        s->buffer_attr_req = a;

        fix_playback_buffer_attr(s);
        pa_assert_se(pa_asyncmsgq_send(s->sink_input->sink->asyncmsgq, PA_MSGOBJECT(s->sink_input), SINK_INPUT_MESSAGE_UPDATE_BUFFER_ATTR, NULL, 0, NULL) == 0);

        reply = reply_new(tag);
        pa_tagstruct_putu32(reply, s->buffer_attr.maxlength);
        pa_tagstruct_putu32(reply, s->buffer_attr.tlength);
        pa_tagstruct_putu32(reply, s->buffer_attr.prebuf);
        pa_tagstruct_putu32(reply, s->buffer_attr.minreq);

        if (c->version >= 13)
            pa_tagstruct_put_usec(reply, s->configured_sink_latency);

    } else {
        record_stream *s;
        bool adjust_latency = false, early_requests = false;
        pa_assert(command == PA_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR);

        s = pa_idxset_get_by_index(c->record_streams, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);

        if (pa_tagstruct_get(
                    t,
                    PA_TAG_U32, &a.maxlength,
                    PA_TAG_U32, &a.fragsize,
                    PA_TAG_INVALID) < 0 ||
            (c->version >= 13 && pa_tagstruct_get_boolean(t, &adjust_latency) < 0) ||
            (c->version >= 14 && pa_tagstruct_get_boolean(t, &early_requests) < 0) ||
            !pa_tagstruct_eof(t)) {
            protocol_error(c);
            return;
        }

        s->adjust_latency = adjust_latency;
        s->early_requests = early_requests;
        s->buffer_attr_req = a;

        fix_record_buffer_attr_pre(s);
        pa_memblockq_set_maxlength(s->memblockq, s->buffer_attr.maxlength);
        pa_memblockq_get_attr(s->memblockq, &s->buffer_attr);
        fix_record_buffer_attr_post(s);

        reply = reply_new(tag);
        pa_tagstruct_putu32(reply, s->buffer_attr.maxlength);
        pa_tagstruct_putu32(reply, s->buffer_attr.fragsize);

        if (c->version >= 13)
            pa_tagstruct_put_usec(reply, s->configured_source_latency);
    }

    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_update_stream_sample_rate(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    uint32_t rate;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_getu32(t, &rate) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, pa_sample_rate_valid(rate), tag, PA_ERR_INVALID);

    if (command == PA_COMMAND_UPDATE_PLAYBACK_STREAM_SAMPLE_RATE) {
        playback_stream *s;

        s = pa_idxset_get_by_index(c->output_streams, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);
        CHECK_VALIDITY(c->pstream, playback_stream_isinstance(s), tag, PA_ERR_NOENTITY);

        pa_sink_input_set_rate(s->sink_input, rate);

    } else {
        record_stream *s;
        pa_assert(command == PA_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE);

        s = pa_idxset_get_by_index(c->record_streams, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);

        pa_source_output_set_rate(s->source_output, rate);
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_update_proplist(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    uint32_t mode;
    pa_proplist *p;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    p = pa_proplist_new();

    if (command == PA_COMMAND_UPDATE_CLIENT_PROPLIST) {

        if (pa_tagstruct_getu32(t, &mode) < 0 ||
            pa_tagstruct_get_proplist(t, p) < 0 ||
            !pa_tagstruct_eof(t)) {
            protocol_error(c);
            pa_proplist_free(p);
            return;
        }

    } else {

        if (pa_tagstruct_getu32(t, &idx) < 0 ||
            pa_tagstruct_getu32(t, &mode) < 0 ||
            pa_tagstruct_get_proplist(t, p) < 0 ||
            !pa_tagstruct_eof(t)) {
            protocol_error(c);
            pa_proplist_free(p);
            return;
        }
    }

    if (!(mode == PA_UPDATE_SET || mode == PA_UPDATE_MERGE || mode == PA_UPDATE_REPLACE)) {
        pa_proplist_free(p);
        CHECK_VALIDITY(c->pstream, false, tag, PA_ERR_INVALID);
    }

    if (command == PA_COMMAND_UPDATE_PLAYBACK_STREAM_PROPLIST) {
        playback_stream *s;

        s = pa_idxset_get_by_index(c->output_streams, idx);
        if (!s || !playback_stream_isinstance(s)) {
            pa_proplist_free(p);
            CHECK_VALIDITY(c->pstream, false, tag, PA_ERR_NOENTITY);
        }
        pa_sink_input_update_proplist(s->sink_input, mode, p);

    } else if (command == PA_COMMAND_UPDATE_RECORD_STREAM_PROPLIST) {
        record_stream *s;

        if (!(s = pa_idxset_get_by_index(c->record_streams, idx))) {
            pa_proplist_free(p);
            CHECK_VALIDITY(c->pstream, false, tag, PA_ERR_NOENTITY);
        }
        pa_source_output_update_proplist(s->source_output, mode, p);

    } else {
        pa_assert(command == PA_COMMAND_UPDATE_CLIENT_PROPLIST);

        pa_client_update_proplist(c->client, mode, p);
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
    pa_proplist_free(p);
}

static void command_remove_proplist(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    unsigned changed = 0;
    pa_proplist *p;
    pa_strlist *l = NULL;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    if (command != PA_COMMAND_REMOVE_CLIENT_PROPLIST) {

        if (pa_tagstruct_getu32(t, &idx) < 0) {
            protocol_error(c);
            return;
        }
    }

    if (command == PA_COMMAND_REMOVE_PLAYBACK_STREAM_PROPLIST) {
        playback_stream *s;

        s = pa_idxset_get_by_index(c->output_streams, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);
        CHECK_VALIDITY(c->pstream, playback_stream_isinstance(s), tag, PA_ERR_NOENTITY);

        p = s->sink_input->proplist;

    } else if (command == PA_COMMAND_REMOVE_RECORD_STREAM_PROPLIST) {
        record_stream *s;

        s = pa_idxset_get_by_index(c->record_streams, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);

        p = s->source_output->proplist;
    } else {
        pa_assert(command == PA_COMMAND_REMOVE_CLIENT_PROPLIST);

        p = c->client->proplist;
    }

    for (;;) {
        const char *k;

        if (pa_tagstruct_gets(t, &k) < 0) {
            protocol_error(c);
            pa_strlist_free(l);
            return;
        }

        if (!k)
            break;

        l = pa_strlist_prepend(l, k);
    }

    if (!pa_tagstruct_eof(t)) {
        protocol_error(c);
        pa_strlist_free(l);
        return;
    }

    for (;;) {
        char *z;

        l = pa_strlist_pop(l, &z);

        if (!z)
            break;

        changed += (unsigned) (pa_proplist_unset(p, z) >= 0);
        pa_xfree(z);
    }

    pa_pstream_send_simple_ack(c->pstream, tag);

    if (changed) {
        if (command == PA_COMMAND_REMOVE_PLAYBACK_STREAM_PROPLIST) {
            playback_stream *s;

            s = pa_idxset_get_by_index(c->output_streams, idx);
            pa_subscription_post(c->protocol->core, PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_CHANGE, s->sink_input->index);

        } else if (command == PA_COMMAND_REMOVE_RECORD_STREAM_PROPLIST) {
            record_stream *s;

            s = pa_idxset_get_by_index(c->record_streams, idx);
            pa_subscription_post(c->protocol->core, PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT|PA_SUBSCRIPTION_EVENT_CHANGE, s->source_output->index);

        } else {
            pa_assert(command == PA_COMMAND_REMOVE_CLIENT_PROPLIST);
            pa_subscription_post(c->protocol->core, PA_SUBSCRIPTION_EVENT_CLIENT|PA_SUBSCRIPTION_EVENT_CHANGE, c->client->index);
        }
    }
}

static void command_set_default_sink_or_source(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    const char *s;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_gets(t, &s) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, !s || pa_namereg_is_valid_name(s), tag, PA_ERR_INVALID);

    if (command == PA_COMMAND_SET_DEFAULT_SOURCE) {
        pa_source *source;

        source = pa_namereg_get(c->protocol->core, s, PA_NAMEREG_SOURCE);
        CHECK_VALIDITY(c->pstream, source, tag, PA_ERR_NOENTITY);

        pa_core_set_configured_default_source(c->protocol->core, source->name);
    } else {
        pa_sink *sink;
        pa_assert(command == PA_COMMAND_SET_DEFAULT_SINK);

        sink = pa_namereg_get(c->protocol->core, s, PA_NAMEREG_SINK);
        CHECK_VALIDITY(c->pstream, sink, tag, PA_ERR_NOENTITY);

        pa_core_set_configured_default_sink(c->protocol->core, sink->name);
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_set_stream_name(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    const char *name;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_gets(t, &name) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, name && pa_utf8_valid(name), tag, PA_ERR_INVALID);

    if (command == PA_COMMAND_SET_PLAYBACK_STREAM_NAME) {
        playback_stream *s;

        s = pa_idxset_get_by_index(c->output_streams, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);
        CHECK_VALIDITY(c->pstream, playback_stream_isinstance(s), tag, PA_ERR_NOENTITY);

        pa_sink_input_set_property(s->sink_input, PA_PROP_MEDIA_NAME, name);

    } else {
        record_stream *s;
        pa_assert(command == PA_COMMAND_SET_RECORD_STREAM_NAME);

        s = pa_idxset_get_by_index(c->record_streams, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);

        pa_source_output_set_property(s->source_output, PA_PROP_MEDIA_NAME, name);
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_kill(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);

    if (command == PA_COMMAND_KILL_CLIENT) {
        pa_client *client;

        client = pa_idxset_get_by_index(c->protocol->core->clients, idx);
        CHECK_VALIDITY(c->pstream, client, tag, PA_ERR_NOENTITY);

        pa_native_connection_ref(c);
        pa_client_kill(client);

    } else if (command == PA_COMMAND_KILL_SINK_INPUT) {
        pa_sink_input *s;

        s = pa_idxset_get_by_index(c->protocol->core->sink_inputs, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);

        pa_native_connection_ref(c);
        pa_sink_input_kill(s);
    } else {
        pa_source_output *s;

        pa_assert(command == PA_COMMAND_KILL_SOURCE_OUTPUT);

        s = pa_idxset_get_by_index(c->protocol->core->source_outputs, idx);
        CHECK_VALIDITY(c->pstream, s, tag, PA_ERR_NOENTITY);

        pa_native_connection_ref(c);
        pa_source_output_kill(s);
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
    pa_native_connection_unref(c);
}

static void command_load_module(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    pa_module *m;
    const char *name, *argument;
    pa_tagstruct *reply;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_gets(t, &name) < 0 ||
        pa_tagstruct_gets(t, &argument) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, name && *name && pa_utf8_valid(name) && !strchr(name, '/'), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, !argument || pa_utf8_valid(argument), tag, PA_ERR_INVALID);

    if (pa_module_load(&m, c->protocol->core, name, argument) < 0) {
        pa_pstream_send_error(c->pstream, tag, PA_ERR_MODINITFAILED);
        return;
    }

    reply = reply_new(tag);
    pa_tagstruct_putu32(reply, m->index);
    pa_pstream_send_tagstruct(c->pstream, reply);
}

static void command_unload_module(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx;
    pa_module *m;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    m = pa_idxset_get_by_index(c->protocol->core->modules, idx);
    CHECK_VALIDITY(c->pstream, m, tag, PA_ERR_NOENTITY);

    pa_module_unload_request(m, false);
    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_move_stream(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx = PA_INVALID_INDEX, idx_device = PA_INVALID_INDEX;
    const char *name_device = NULL;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_getu32(t, &idx_device) < 0 ||
        pa_tagstruct_gets(t, &name_device) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, idx != PA_INVALID_INDEX, tag, PA_ERR_INVALID);

    CHECK_VALIDITY(c->pstream, !name_device || pa_namereg_is_valid_name_or_wildcard(name_device, command == PA_COMMAND_MOVE_SINK_INPUT ? PA_NAMEREG_SINK : PA_NAMEREG_SOURCE), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, (idx_device != PA_INVALID_INDEX) ^ (name_device != NULL), tag, PA_ERR_INVALID);

    if (command == PA_COMMAND_MOVE_SINK_INPUT) {
        pa_sink_input *si = NULL;
        pa_sink *sink = NULL;

        si = pa_idxset_get_by_index(c->protocol->core->sink_inputs, idx);

        if (idx_device != PA_INVALID_INDEX)
            sink = pa_idxset_get_by_index(c->protocol->core->sinks, idx_device);
        else
            sink = pa_namereg_get(c->protocol->core, name_device, PA_NAMEREG_SINK);

        CHECK_VALIDITY(c->pstream, si && sink, tag, PA_ERR_NOENTITY);

        if (pa_sink_input_move_to(si, sink, true) < 0) {
            pa_pstream_send_error(c->pstream, tag, PA_ERR_INVALID);
            return;
        }
    } else {
        pa_source_output *so = NULL;
        pa_source *source;

        pa_assert(command == PA_COMMAND_MOVE_SOURCE_OUTPUT);

        so = pa_idxset_get_by_index(c->protocol->core->source_outputs, idx);

        if (idx_device != PA_INVALID_INDEX)
            source = pa_idxset_get_by_index(c->protocol->core->sources, idx_device);
        else
            source = pa_namereg_get(c->protocol->core, name_device, PA_NAMEREG_SOURCE);

        CHECK_VALIDITY(c->pstream, so && source, tag, PA_ERR_NOENTITY);

        if (pa_source_output_move_to(so, source, true) < 0) {
            pa_pstream_send_error(c->pstream, tag, PA_ERR_INVALID);
            return;
        }
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_suspend(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx = PA_INVALID_INDEX;
    const char *name = NULL;
    bool b;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_gets(t, &name) < 0 ||
        pa_tagstruct_get_boolean(t, &b) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, !name || pa_namereg_is_valid_name_or_wildcard(name, command == PA_COMMAND_SUSPEND_SINK ? PA_NAMEREG_SINK : PA_NAMEREG_SOURCE) || *name == 0, tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, (idx != PA_INVALID_INDEX) ^ (name != NULL), tag, PA_ERR_INVALID);

    if (command == PA_COMMAND_SUSPEND_SINK) {

        if (idx == PA_INVALID_INDEX && name && !*name) {

            pa_log_debug("%s all sinks", b ? "Suspending" : "Resuming");

            if (pa_sink_suspend_all(c->protocol->core, b, PA_SUSPEND_USER) < 0) {
                pa_pstream_send_error(c->pstream, tag, PA_ERR_INVALID);
                return;
            }
        } else {
            pa_sink *sink = NULL;

            if (idx != PA_INVALID_INDEX)
                sink = pa_idxset_get_by_index(c->protocol->core->sinks, idx);
            else
                sink = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SINK);

            CHECK_VALIDITY(c->pstream, sink, tag, PA_ERR_NOENTITY);

            pa_log_debug("%s of sink %s requested by client %" PRIu32 ".",
                         b ? "Suspending" : "Resuming", sink->name, c->client->index);

            if (pa_sink_suspend(sink, b, PA_SUSPEND_USER) < 0) {
                pa_pstream_send_error(c->pstream, tag, PA_ERR_INVALID);
                return;
            }
        }
    } else {

        pa_assert(command == PA_COMMAND_SUSPEND_SOURCE);

        if (idx == PA_INVALID_INDEX && name && !*name) {

            pa_log_debug("%s all sources", b ? "Suspending" : "Resuming");

            if (pa_source_suspend_all(c->protocol->core, b, PA_SUSPEND_USER) < 0) {
                pa_pstream_send_error(c->pstream, tag, PA_ERR_INVALID);
                return;
            }

        } else {
            pa_source *source;

            if (idx != PA_INVALID_INDEX)
                source = pa_idxset_get_by_index(c->protocol->core->sources, idx);
            else
                source = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SOURCE);

            CHECK_VALIDITY(c->pstream, source, tag, PA_ERR_NOENTITY);

            pa_log_debug("%s of source %s requested by client %" PRIu32 ".",
                         b ? "Suspending" : "Resuming", source->name, c->client->index);

            if (pa_source_suspend(source, b, PA_SUSPEND_USER) < 0) {
                pa_pstream_send_error(c->pstream, tag, PA_ERR_INVALID);
                return;
            }
        }
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_extension(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx = PA_INVALID_INDEX;
    const char *name = NULL;
    pa_module *m;
    pa_native_protocol_ext_cb_t cb;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_gets(t, &name) < 0) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, !name || pa_utf8_valid(name), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, (idx != PA_INVALID_INDEX) ^ (name != NULL), tag, PA_ERR_INVALID);

    if (idx != PA_INVALID_INDEX)
        m = pa_idxset_get_by_index(c->protocol->core->modules, idx);
    else
        PA_IDXSET_FOREACH(m, c->protocol->core->modules, idx)
            if (pa_streq(name, m->name))
                break;

    CHECK_VALIDITY(c->pstream, m, tag, PA_ERR_NOEXTENSION);
    CHECK_VALIDITY(c->pstream, m->load_once || idx != PA_INVALID_INDEX, tag, PA_ERR_INVALID);

    cb = (pa_native_protocol_ext_cb_t) (unsigned long) pa_hashmap_get(c->protocol->extensions, m);
    CHECK_VALIDITY(c->pstream, cb, tag, PA_ERR_NOEXTENSION);

    if (cb(c->protocol, m, c, tag, t) < 0)
        protocol_error(c);
}

static void command_set_card_profile(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx = PA_INVALID_INDEX;
    const char *name = NULL, *profile_name = NULL;
    pa_card *card = NULL;
    pa_card_profile *profile;
    int ret;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_gets(t, &name) < 0 ||
        pa_tagstruct_gets(t, &profile_name) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, !name || pa_namereg_is_valid_name(name), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, (idx != PA_INVALID_INDEX) ^ (name != NULL), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, profile_name, tag, PA_ERR_INVALID);

    if (idx != PA_INVALID_INDEX)
        card = pa_idxset_get_by_index(c->protocol->core->cards, idx);
    else
        card = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_CARD);

    CHECK_VALIDITY(c->pstream, card, tag, PA_ERR_NOENTITY);

    profile = pa_hashmap_get(card->profiles, profile_name);

    CHECK_VALIDITY(c->pstream, profile, tag, PA_ERR_NOENTITY);

    pa_log_info("Application \"%s\" requests card profile change. card = %s, profile = %s",
                pa_strnull(pa_proplist_gets(c->client->proplist, PA_PROP_APPLICATION_NAME)),
                card->name,
                profile->name);

    if ((ret = pa_card_set_profile(card, profile, true)) < 0) {
        pa_pstream_send_error(c->pstream, tag, -ret);
        return;
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_set_sink_or_source_port(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    uint32_t idx = PA_INVALID_INDEX;
    const char *name = NULL, *port = NULL;
    int ret;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_gets(t, &name) < 0 ||
        pa_tagstruct_gets(t, &port) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, !name || pa_namereg_is_valid_name_or_wildcard(name, command == PA_COMMAND_SET_SINK_PORT ? PA_NAMEREG_SINK : PA_NAMEREG_SOURCE), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, (idx != PA_INVALID_INDEX) ^ (name != NULL), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, port, tag, PA_ERR_INVALID);

    if (command == PA_COMMAND_SET_SINK_PORT) {
        pa_sink *sink;

        if (idx != PA_INVALID_INDEX)
            sink = pa_idxset_get_by_index(c->protocol->core->sinks, idx);
        else
            sink = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SINK);

        CHECK_VALIDITY(c->pstream, sink, tag, PA_ERR_NOENTITY);

        if ((ret = pa_sink_set_port(sink, port, true)) < 0) {
            pa_pstream_send_error(c->pstream, tag, -ret);
            return;
        }
    } else {
        pa_source *source;

        pa_assert(command == PA_COMMAND_SET_SOURCE_PORT);

        if (idx != PA_INVALID_INDEX)
            source = pa_idxset_get_by_index(c->protocol->core->sources, idx);
        else
            source = pa_namereg_get(c->protocol->core, name, PA_NAMEREG_SOURCE);

        CHECK_VALIDITY(c->pstream, source, tag, PA_ERR_NOENTITY);

        if ((ret = pa_source_set_port(source, port, true)) < 0) {
            pa_pstream_send_error(c->pstream, tag, -ret);
            return;
        }
    }

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static void command_set_port_latency_offset(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    const char *port_name, *card_name;
    uint32_t idx = PA_INVALID_INDEX;
    int64_t offset;
    pa_card *card = NULL;
    pa_device_port *port = NULL;

    pa_native_connection_assert_ref(c);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_gets(t, &card_name) < 0 ||
        pa_tagstruct_gets(t, &port_name) < 0 ||
        pa_tagstruct_gets64(t, &offset) < 0 ||
        !pa_tagstruct_eof(t)) {
        protocol_error(c);
        return;
    }

    CHECK_VALIDITY(c->pstream, c->authorized, tag, PA_ERR_ACCESS);
    CHECK_VALIDITY(c->pstream, !card_name || pa_namereg_is_valid_name(card_name), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, (idx != PA_INVALID_INDEX) ^ (card_name != NULL), tag, PA_ERR_INVALID);
    CHECK_VALIDITY(c->pstream, port_name, tag, PA_ERR_INVALID);

    if (idx != PA_INVALID_INDEX)
        card = pa_idxset_get_by_index(c->protocol->core->cards, idx);
    else
        card = pa_namereg_get(c->protocol->core, card_name, PA_NAMEREG_CARD);

    CHECK_VALIDITY(c->pstream, card, tag, PA_ERR_NOENTITY);

    port = pa_hashmap_get(card->ports, port_name);
    CHECK_VALIDITY(c->pstream, port, tag, PA_ERR_NOENTITY);

    pa_device_port_set_latency_offset(port, offset);

    pa_pstream_send_simple_ack(c->pstream, tag);
}

static const pa_pdispatch_cb_t command_table[PA_COMMAND_MAX] = {
    [PA_COMMAND_ERROR] = NULL,
    [PA_COMMAND_TIMEOUT] = NULL,
    [PA_COMMAND_REPLY] = NULL,
    [PA_COMMAND_CREATE_PLAYBACK_STREAM] = command_create_playback_stream,
    [PA_COMMAND_DELETE_PLAYBACK_STREAM] = command_delete_stream,
    [PA_COMMAND_DRAIN_PLAYBACK_STREAM] = command_drain_playback_stream,
    [PA_COMMAND_CREATE_RECORD_STREAM] = command_create_record_stream,
    [PA_COMMAND_DELETE_RECORD_STREAM] = command_delete_stream,
    [PA_COMMAND_AUTH] = command_auth,
    [PA_COMMAND_REQUEST] = NULL,
    [PA_COMMAND_EXIT] = command_exit,
    [PA_COMMAND_SET_CLIENT_NAME] = command_set_client_name,
    [PA_COMMAND_LOOKUP_SINK] = command_lookup,
    [PA_COMMAND_LOOKUP_SOURCE] = command_lookup,
    [PA_COMMAND_STAT] = command_stat,
    [PA_COMMAND_GET_PLAYBACK_LATENCY] = command_get_playback_latency,
    [PA_COMMAND_GET_RECORD_LATENCY] = command_get_record_latency,
    [PA_COMMAND_CREATE_UPLOAD_STREAM] = command_create_upload_stream,
    [PA_COMMAND_DELETE_UPLOAD_STREAM] = command_delete_stream,
    [PA_COMMAND_FINISH_UPLOAD_STREAM] = command_finish_upload_stream,
    [PA_COMMAND_PLAY_SAMPLE] = command_play_sample,
    [PA_COMMAND_REMOVE_SAMPLE] = command_remove_sample,
    [PA_COMMAND_GET_SINK_INFO] = command_get_info,
    [PA_COMMAND_GET_SOURCE_INFO] = command_get_info,
    [PA_COMMAND_GET_CLIENT_INFO] = command_get_info,
    [PA_COMMAND_GET_CARD_INFO] = command_get_info,
    [PA_COMMAND_GET_MODULE_INFO] = command_get_info,
    [PA_COMMAND_GET_SINK_INPUT_INFO] = command_get_info,
    [PA_COMMAND_GET_SOURCE_OUTPUT_INFO] = command_get_info,
    [PA_COMMAND_GET_SAMPLE_INFO] = command_get_info,
    [PA_COMMAND_GET_SINK_INFO_LIST] = command_get_info_list,
    [PA_COMMAND_GET_SOURCE_INFO_LIST] = command_get_info_list,
    [PA_COMMAND_GET_MODULE_INFO_LIST] = command_get_info_list,
    [PA_COMMAND_GET_CLIENT_INFO_LIST] = command_get_info_list,
    [PA_COMMAND_GET_CARD_INFO_LIST] = command_get_info_list,
    [PA_COMMAND_GET_SINK_INPUT_INFO_LIST] = command_get_info_list,
    [PA_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST] = command_get_info_list,
    [PA_COMMAND_GET_SAMPLE_INFO_LIST] = command_get_info_list,
    [PA_COMMAND_GET_SERVER_INFO] = command_get_server_info,
    [PA_COMMAND_SUBSCRIBE] = command_subscribe,

    [PA_COMMAND_SET_SINK_VOLUME] = command_set_volume,
    [PA_COMMAND_SET_SINK_INPUT_VOLUME] = command_set_volume,
    [PA_COMMAND_SET_SOURCE_VOLUME] = command_set_volume,
    [PA_COMMAND_SET_SOURCE_OUTPUT_VOLUME] = command_set_volume,

    [PA_COMMAND_SET_SINK_MUTE] = command_set_mute,
    [PA_COMMAND_SET_SINK_INPUT_MUTE] = command_set_mute,
    [PA_COMMAND_SET_SOURCE_MUTE] = command_set_mute,
    [PA_COMMAND_SET_SOURCE_OUTPUT_MUTE] = command_set_mute,

    [PA_COMMAND_SUSPEND_SINK] = command_suspend,
    [PA_COMMAND_SUSPEND_SOURCE] = command_suspend,

    [PA_COMMAND_CORK_PLAYBACK_STREAM] = command_cork_playback_stream,
    [PA_COMMAND_FLUSH_PLAYBACK_STREAM] = command_trigger_or_flush_or_prebuf_playback_stream,
    [PA_COMMAND_TRIGGER_PLAYBACK_STREAM] = command_trigger_or_flush_or_prebuf_playback_stream,
    [PA_COMMAND_PREBUF_PLAYBACK_STREAM] = command_trigger_or_flush_or_prebuf_playback_stream,

    [PA_COMMAND_CORK_RECORD_STREAM] = command_cork_record_stream,
    [PA_COMMAND_FLUSH_RECORD_STREAM] = command_flush_record_stream,

    [PA_COMMAND_SET_DEFAULT_SINK] = command_set_default_sink_or_source,
    [PA_COMMAND_SET_DEFAULT_SOURCE] = command_set_default_sink_or_source,
    [PA_COMMAND_SET_PLAYBACK_STREAM_NAME] = command_set_stream_name,
    [PA_COMMAND_SET_RECORD_STREAM_NAME] = command_set_stream_name,
    [PA_COMMAND_KILL_CLIENT] = command_kill,
    [PA_COMMAND_KILL_SINK_INPUT] = command_kill,
    [PA_COMMAND_KILL_SOURCE_OUTPUT] = command_kill,
    [PA_COMMAND_LOAD_MODULE] = command_load_module,
    [PA_COMMAND_UNLOAD_MODULE] = command_unload_module,

    [PA_COMMAND_GET_AUTOLOAD_INFO___OBSOLETE] = NULL,
    [PA_COMMAND_GET_AUTOLOAD_INFO_LIST___OBSOLETE] = NULL,
    [PA_COMMAND_ADD_AUTOLOAD___OBSOLETE] = NULL,
    [PA_COMMAND_REMOVE_AUTOLOAD___OBSOLETE] = NULL,

    [PA_COMMAND_MOVE_SINK_INPUT] = command_move_stream,
    [PA_COMMAND_MOVE_SOURCE_OUTPUT] = command_move_stream,

    [PA_COMMAND_SET_PLAYBACK_STREAM_BUFFER_ATTR] = command_set_stream_buffer_attr,
    [PA_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR] = command_set_stream_buffer_attr,

    [PA_COMMAND_UPDATE_PLAYBACK_STREAM_SAMPLE_RATE] = command_update_stream_sample_rate,
    [PA_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE] = command_update_stream_sample_rate,

    [PA_COMMAND_UPDATE_RECORD_STREAM_PROPLIST] = command_update_proplist,
    [PA_COMMAND_UPDATE_PLAYBACK_STREAM_PROPLIST] = command_update_proplist,
    [PA_COMMAND_UPDATE_CLIENT_PROPLIST] = command_update_proplist,

    [PA_COMMAND_REMOVE_RECORD_STREAM_PROPLIST] = command_remove_proplist,
    [PA_COMMAND_REMOVE_PLAYBACK_STREAM_PROPLIST] = command_remove_proplist,
    [PA_COMMAND_REMOVE_CLIENT_PROPLIST] = command_remove_proplist,

    [PA_COMMAND_SET_CARD_PROFILE] = command_set_card_profile,

    [PA_COMMAND_SET_SINK_PORT] = command_set_sink_or_source_port,
    [PA_COMMAND_SET_SOURCE_PORT] = command_set_sink_or_source_port,

    [PA_COMMAND_SET_PORT_LATENCY_OFFSET] = command_set_port_latency_offset,

    [PA_COMMAND_ENABLE_SRBCHANNEL] = command_enable_srbchannel,

    [PA_COMMAND_REGISTER_MEMFD_SHMID] = command_register_memfd_shmid,

    [PA_COMMAND_EXTENSION] = command_extension
};

/*** pstream callbacks ***/

static void pstream_packet_callback(pa_pstream *p, pa_packet *packet, pa_cmsg_ancil_data *ancil_data, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);

    pa_assert(p);
    pa_assert(packet);
    pa_native_connection_assert_ref(c);

    if (pa_pdispatch_run(c->pdispatch, packet, ancil_data, c) < 0) {
        pa_log("invalid packet.");
        native_connection_unlink(c);
    }
}

static void pstream_memblock_callback(pa_pstream *p, uint32_t channel, int64_t offset, pa_seek_mode_t seek, const pa_memchunk *chunk, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);
    output_stream *stream;

    pa_assert(p);
    pa_assert(chunk);
    pa_native_connection_assert_ref(c);

    if (!(stream = OUTPUT_STREAM(pa_idxset_get_by_index(c->output_streams, channel)))) {
        pa_log_debug("Client sent block for invalid stream.");
        /* Ignoring */
        return;
    }

#ifdef PROTOCOL_NATIVE_DEBUG
    pa_log("got %lu bytes from client", (unsigned long) chunk->length);
#endif

    if (playback_stream_isinstance(stream)) {
        playback_stream *ps = PLAYBACK_STREAM(stream);

        size_t frame_size = pa_frame_size(&ps->sink_input->sample_spec);
        if (chunk->index % frame_size != 0 || chunk->length % frame_size != 0) {
            pa_log_warn("Client sent non-aligned memblock: index %d, length %d, frame size: %d",
                        (int) chunk->index, (int) chunk->length, (int) frame_size);
            return;
        }

        pa_atomic_inc(&ps->seek_or_post_in_queue);
        if (chunk->memblock) {
            if (seek != PA_SEEK_RELATIVE || offset != 0)
                pa_asyncmsgq_post(ps->sink_input->sink->asyncmsgq, PA_MSGOBJECT(ps->sink_input), SINK_INPUT_MESSAGE_SEEK, PA_UINT_TO_PTR(seek), offset, chunk, NULL);
            else
                pa_asyncmsgq_post(ps->sink_input->sink->asyncmsgq, PA_MSGOBJECT(ps->sink_input), SINK_INPUT_MESSAGE_POST_DATA, NULL, 0, chunk, NULL);
        } else
            pa_asyncmsgq_post(ps->sink_input->sink->asyncmsgq, PA_MSGOBJECT(ps->sink_input), SINK_INPUT_MESSAGE_SEEK, PA_UINT_TO_PTR(seek), offset+chunk->length, NULL, NULL);

    } else {
        upload_stream *u = UPLOAD_STREAM(stream);
        size_t l;

        if (!u->memchunk.memblock) {
            if (u->length == chunk->length && chunk->memblock) {
                u->memchunk = *chunk;
                pa_memblock_ref(u->memchunk.memblock);
                u->length = 0;
            } else {
                u->memchunk.memblock = pa_memblock_new(c->protocol->core->mempool, u->length);
                u->memchunk.index = u->memchunk.length = 0;
            }
        }

        pa_assert(u->memchunk.memblock);

        l = u->length;
        if (l > chunk->length)
            l = chunk->length;

        if (l > 0) {
            void *dst;
            dst = pa_memblock_acquire(u->memchunk.memblock);

            if (chunk->memblock) {
                void *src;
                src = pa_memblock_acquire(chunk->memblock);

                memcpy((uint8_t*) dst + u->memchunk.index + u->memchunk.length,
                       (uint8_t*) src + chunk->index, l);

                pa_memblock_release(chunk->memblock);
            } else
                pa_silence_memory((uint8_t*) dst + u->memchunk.index + u->memchunk.length, l, &u->sample_spec);

            pa_memblock_release(u->memchunk.memblock);

            u->memchunk.length += l;
            u->length -= l;
        }
    }
}

static void pstream_die_callback(pa_pstream *p, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);

    pa_assert(p);
    pa_native_connection_assert_ref(c);

    native_connection_unlink(c);
    pa_log_info("Connection died.");
}

static void pstream_drain_callback(pa_pstream *p, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);

    pa_assert(p);
    pa_native_connection_assert_ref(c);

    native_connection_send_memblock(c);
}

static void pstream_revoke_callback(pa_pstream *p, uint32_t block_id, void *userdata) {
    pa_thread_mq *q;

    if (!(q = pa_thread_mq_get()))
        pa_pstream_send_revoke(p, block_id);
    else
        pa_asyncmsgq_post(q->outq, PA_MSGOBJECT(userdata), CONNECTION_MESSAGE_REVOKE, PA_UINT_TO_PTR(block_id), 0, NULL, NULL);
}

static void pstream_release_callback(pa_pstream *p, uint32_t block_id, void *userdata) {
    pa_thread_mq *q;

    if (!(q = pa_thread_mq_get()))
        pa_pstream_send_release(p, block_id);
    else
        pa_asyncmsgq_post(q->outq, PA_MSGOBJECT(userdata), CONNECTION_MESSAGE_RELEASE, PA_UINT_TO_PTR(block_id), 0, NULL, NULL);
}

/*** client callbacks ***/

static void client_kill_cb(pa_client *c) {
    pa_assert(c);

    native_connection_unlink(PA_NATIVE_CONNECTION(c->userdata));
    pa_log_info("Connection killed.");
}

static void client_send_event_cb(pa_client *client, const char*event, pa_proplist *pl) {
    pa_tagstruct *t;
    pa_native_connection *c;

    pa_assert(client);
    c = PA_NATIVE_CONNECTION(client->userdata);
    pa_native_connection_assert_ref(c);

    if (c->version < 15)
      return;

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_CLIENT_EVENT);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_puts(t, event);
    pa_tagstruct_put_proplist(t, pl);
    pa_pstream_send_tagstruct(c->pstream, t);
}

/*** module entry points ***/

static void auth_timeout(pa_mainloop_api*m, pa_time_event *e, const struct timeval *t, void *userdata) {
    pa_native_connection *c = PA_NATIVE_CONNECTION(userdata);

    pa_assert(m);
    pa_native_connection_assert_ref(c);
    pa_assert(c->auth_timeout_event == e);

    if (!c->authorized) {
        native_connection_unlink(c);
        pa_log_info("Connection terminated due to authentication timeout.");
    }
}

void pa_native_protocol_connect(pa_native_protocol *p, pa_iochannel *io, pa_native_options *o) {
    pa_native_connection *c;
    char pname[128];
    pa_client *client;
    pa_client_new_data data;

    pa_assert(p);
    pa_assert(io);
    pa_assert(o);

    if (pa_idxset_size(p->connections)+1 > MAX_CONNECTIONS) {
        pa_log_warn("Warning! Too many connections (%u), dropping incoming connection.", MAX_CONNECTIONS);
        pa_iochannel_free(io);
        return;
    }

    pa_client_new_data_init(&data);
    data.module = o->module;
    data.driver = __FILE__;
    pa_iochannel_socket_peer_to_string(io, pname, sizeof(pname));
    pa_proplist_setf(data.proplist, PA_PROP_APPLICATION_NAME, "Native client (%s)", pname);
    pa_proplist_sets(data.proplist, "native-protocol.peer", pname);
    client = pa_client_new(p->core, &data);
    pa_client_new_data_done(&data);

    if (!client)
        return;

    c = pa_msgobject_new(pa_native_connection);
    c->parent.parent.free = native_connection_free;
    c->parent.process_msg = native_connection_process_msg;
    c->protocol = p;
    c->options = pa_native_options_ref(o);
    c->authorized = false;
    c->srbpending = NULL;

    if (o->auth_anonymous) {
        pa_log_info("Client authenticated anonymously.");
        c->authorized = true;
    }

    if (!c->authorized &&
        o->auth_ip_acl &&
        pa_ip_acl_check(o->auth_ip_acl, pa_iochannel_get_recv_fd(io)) > 0) {

        pa_log_info("Client authenticated by IP ACL.");
        c->authorized = true;
    }

    if (!c->authorized)
        c->auth_timeout_event = pa_core_rttime_new(p->core, pa_rtclock_now() + AUTH_TIMEOUT, auth_timeout, c);
    else
        c->auth_timeout_event = NULL;

    c->is_local = pa_iochannel_socket_is_local(io);
    c->version = 8;

    c->client = client;
    c->client->kill = client_kill_cb;
    c->client->send_event = client_send_event_cb;
    c->client->userdata = c;

    c->rw_mempool = NULL;

    c->pstream = pa_pstream_new(p->core->mainloop, io, p->core->mempool);
    pa_pstream_set_receive_packet_callback(c->pstream, pstream_packet_callback, c);
    pa_pstream_set_receive_memblock_callback(c->pstream, pstream_memblock_callback, c);
    pa_pstream_set_die_callback(c->pstream, pstream_die_callback, c);
    pa_pstream_set_drain_callback(c->pstream, pstream_drain_callback, c);
    pa_pstream_set_revoke_callback(c->pstream, pstream_revoke_callback, c);
    pa_pstream_set_release_callback(c->pstream, pstream_release_callback, c);

    c->pdispatch = pa_pdispatch_new(p->core->mainloop, true, command_table, PA_COMMAND_MAX);

    c->record_streams = pa_idxset_new(NULL, NULL);
    c->output_streams = pa_idxset_new(NULL, NULL);

    c->rrobin_index = PA_IDXSET_INVALID;
    c->subscription = NULL;

    pa_idxset_put(p->connections, c, NULL);

#ifdef HAVE_CREDS
    if (pa_iochannel_creds_supported(io))
        pa_iochannel_creds_enable(io);
#endif

    pa_hook_fire(&p->hooks[PA_NATIVE_HOOK_CONNECTION_PUT], c);
}

void pa_native_protocol_disconnect(pa_native_protocol *p, pa_module *m) {
    pa_native_connection *c;
    void *state = NULL;

    pa_assert(p);
    pa_assert(m);

    while ((c = pa_idxset_iterate(p->connections, &state, NULL)))
        if (c->options->module == m)
            native_connection_unlink(c);
}

static pa_native_protocol* native_protocol_new(pa_core *c) {
    pa_native_protocol *p;
    pa_native_hook_t h;

    pa_assert(c);

    p = pa_xnew(pa_native_protocol, 1);
    PA_REFCNT_INIT(p);
    p->core = c;
    p->connections = pa_idxset_new(NULL, NULL);

    p->servers = NULL;

    p->extensions = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    for (h = 0; h < PA_NATIVE_HOOK_MAX; h++)
        pa_hook_init(&p->hooks[h], p);

    pa_assert_se(pa_shared_set(c, "native-protocol", p) >= 0);

    return p;
}

pa_native_protocol* pa_native_protocol_get(pa_core *c) {
    pa_native_protocol *p;

    if ((p = pa_shared_get(c, "native-protocol")))
        return pa_native_protocol_ref(p);

    return native_protocol_new(c);
}

pa_native_protocol* pa_native_protocol_ref(pa_native_protocol *p) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    PA_REFCNT_INC(p);

    return p;
}

void pa_native_protocol_unref(pa_native_protocol *p) {
    pa_native_connection *c;
    pa_native_hook_t h;

    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    if (PA_REFCNT_DEC(p) > 0)
        return;

    while ((c = pa_idxset_first(p->connections, NULL)))
        native_connection_unlink(c);

    pa_idxset_free(p->connections, NULL);

    pa_strlist_free(p->servers);

    for (h = 0; h < PA_NATIVE_HOOK_MAX; h++)
        pa_hook_done(&p->hooks[h]);

    pa_hashmap_free(p->extensions);

    pa_assert_se(pa_shared_remove(p->core, "native-protocol") >= 0);

    pa_xfree(p);
}

void pa_native_protocol_add_server_string(pa_native_protocol *p, const char *name) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);
    pa_assert(name);

    p->servers = pa_strlist_prepend(p->servers, name);

    pa_hook_fire(&p->hooks[PA_NATIVE_HOOK_SERVERS_CHANGED], p->servers);
}

void pa_native_protocol_remove_server_string(pa_native_protocol *p, const char *name) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);
    pa_assert(name);

    p->servers = pa_strlist_remove(p->servers, name);

    pa_hook_fire(&p->hooks[PA_NATIVE_HOOK_SERVERS_CHANGED], p->servers);
}

pa_hook *pa_native_protocol_hooks(pa_native_protocol *p) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    return p->hooks;
}

pa_strlist *pa_native_protocol_servers(pa_native_protocol *p) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    return p->servers;
}

int pa_native_protocol_install_ext(pa_native_protocol *p, pa_module *m, pa_native_protocol_ext_cb_t cb) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);
    pa_assert(m);
    pa_assert(cb);
    pa_assert(!pa_hashmap_get(p->extensions, m));

    pa_assert_se(pa_hashmap_put(p->extensions, m, (void*) (unsigned long) cb) == 0);
    return 0;
}

void pa_native_protocol_remove_ext(pa_native_protocol *p, pa_module *m) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);
    pa_assert(m);

    pa_assert_se(pa_hashmap_remove(p->extensions, m));
}

pa_native_options* pa_native_options_new(void) {
    pa_native_options *o;

    o = pa_xnew0(pa_native_options, 1);
    PA_REFCNT_INIT(o);

    return o;
}

pa_native_options* pa_native_options_ref(pa_native_options *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    PA_REFCNT_INC(o);

    return o;
}

void pa_native_options_unref(pa_native_options *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (PA_REFCNT_DEC(o) > 0)
        return;

    pa_xfree(o->auth_group);

    if (o->auth_ip_acl)
        pa_ip_acl_free(o->auth_ip_acl);

    if (o->auth_cookie)
        pa_auth_cookie_unref(o->auth_cookie);

    pa_xfree(o);
}

int pa_native_options_parse(pa_native_options *o, pa_core *c, pa_modargs *ma) {
    bool enabled;
    const char *acl;

    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);
    pa_assert(ma);

    o->srbchannel = true;
    if (pa_modargs_get_value_boolean(ma, "srbchannel", &o->srbchannel) < 0) {
        pa_log("srbchannel= expects a boolean argument.");
        return -1;
    }

    if (pa_modargs_get_value_boolean(ma, "auth-anonymous", &o->auth_anonymous) < 0) {
        pa_log("auth-anonymous= expects a boolean argument.");
        return -1;
    }

    enabled = true;
    if (pa_modargs_get_value_boolean(ma, "auth-group-enable", &enabled) < 0) {
        pa_log("auth-group-enable= expects a boolean argument.");
        return -1;
    }

    pa_xfree(o->auth_group);
    o->auth_group = enabled ? pa_xstrdup(pa_modargs_get_value(ma, "auth-group", pa_in_system_mode() ? PA_ACCESS_GROUP : NULL)) : NULL;

#ifndef HAVE_CREDS
    if (o->auth_group)
        pa_log_warn("Authentication group configured, but not available on local system. Ignoring.");
#endif

    if ((acl = pa_modargs_get_value(ma, "auth-ip-acl", NULL))) {
        pa_ip_acl *ipa;

        if (!(ipa = pa_ip_acl_new(acl))) {
            pa_log("Failed to parse IP ACL '%s'", acl);
            return -1;
        }

        if (o->auth_ip_acl)
            pa_ip_acl_free(o->auth_ip_acl);

        o->auth_ip_acl = ipa;
    }

    enabled = true;
    if (pa_modargs_get_value_boolean(ma, "auth-cookie-enabled", &enabled) < 0) {
        pa_log("auth-cookie-enabled= expects a boolean argument.");
        return -1;
    }

    if (o->auth_cookie)
        pa_auth_cookie_unref(o->auth_cookie);

    if (enabled) {
        const char *cn;

        /* The new name for this is 'auth-cookie', for compat reasons
         * we check the old name too */
        cn = pa_modargs_get_value(ma, "auth-cookie", NULL);
        if (!cn)
            cn = pa_modargs_get_value(ma, "cookie", NULL);

        if (cn)
            o->auth_cookie = pa_auth_cookie_get(c, cn, true, PA_NATIVE_COOKIE_LENGTH);
        else {
            o->auth_cookie = pa_auth_cookie_get(c, PA_NATIVE_COOKIE_FILE, false, PA_NATIVE_COOKIE_LENGTH);
            if (!o->auth_cookie) {
                char *fallback_path;

                if (pa_append_to_home_dir(PA_NATIVE_COOKIE_FILE_FALLBACK, &fallback_path) >= 0) {
                    o->auth_cookie = pa_auth_cookie_get(c, fallback_path, false, PA_NATIVE_COOKIE_LENGTH);
                    pa_xfree(fallback_path);
                }

                if (!o->auth_cookie)
                    o->auth_cookie = pa_auth_cookie_get(c, PA_NATIVE_COOKIE_FILE, true, PA_NATIVE_COOKIE_LENGTH);
            }
        }

        if (!o->auth_cookie)
            return -1;

    } else
          o->auth_cookie = NULL;

    return 0;
}

pa_pstream* pa_native_connection_get_pstream(pa_native_connection *c) {
    pa_native_connection_assert_ref(c);

    return c->pstream;
}

pa_client* pa_native_connection_get_client(pa_native_connection *c) {
    pa_native_connection_assert_ref(c);

    return c->client;
}
