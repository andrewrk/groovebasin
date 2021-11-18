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

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pulse/rtclock.h>
#include <pulse/sample.h>
#include <pulse/timeval.h>
#include <pulse/utf8.h>
#include <pulse/xmalloc.h>
#include <pulse/proplist.h>

#include <pulsecore/esound.h>
#include <pulsecore/memblock.h>
#include <pulsecore/client.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/sink.h>
#include <pulsecore/source-output.h>
#include <pulsecore/source.h>
#include <pulsecore/core-scache.h>
#include <pulsecore/sample-util.h>
#include <pulsecore/namereg.h>
#include <pulsecore/log.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/ipacl.h>
#include <pulsecore/macro.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/shared.h>
#include <pulsecore/endianmacros.h>

#include "protocol-esound.h"

/* Don't accept more connection than this */
#define MAX_CONNECTIONS 64

/* Kick a client if it doesn't authenticate within this time */
#define AUTH_TIMEOUT (5*PA_USEC_PER_SEC)

#define DEFAULT_COOKIE_FILE ".esd_auth"

#define PLAYBACK_BUFFER_SECONDS (.25)
#define PLAYBACK_BUFFER_FRAGMENTS (10)
#define RECORD_BUFFER_SECONDS (5)

#define MAX_CACHE_SAMPLE_SIZE (2048000)

#define DEFAULT_SINK_LATENCY (150*PA_USEC_PER_MSEC)
#define DEFAULT_SOURCE_LATENCY (150*PA_USEC_PER_MSEC)

#define SCACHE_PREFIX "esound."

/* This is heavily based on esound's code */

typedef struct connection {
    pa_msgobject parent;

    uint32_t index;
    bool dead;
    pa_esound_protocol *protocol;
    pa_esound_options *options;
    pa_iochannel *io;
    pa_client *client;
    bool authorized, swap_byte_order;
    void *write_data;
    size_t write_data_alloc, write_data_index, write_data_length;
    void *read_data;
    size_t read_data_alloc, read_data_length;
    esd_proto_t request;
    esd_client_state_t state;
    pa_sink_input *sink_input;
    pa_source_output *source_output;
    pa_memblockq *input_memblockq, *output_memblockq;
    pa_defer_event *defer_event;

    char *original_name;

    struct {
        pa_memblock *current_memblock;
        size_t memblock_index;
        pa_atomic_t missing;
        bool underrun;
    } playback;

    struct {
        pa_memchunk memchunk;
        char *name;
        pa_sample_spec sample_spec;
    } scache;

    pa_time_event *auth_timeout_event;
} connection;

PA_DEFINE_PRIVATE_CLASS(connection, pa_msgobject);
#define CONNECTION(o) (connection_cast(o))

struct pa_esound_protocol {
    PA_REFCNT_DECLARE;

    pa_core *core;
    pa_idxset *connections;
    unsigned n_player;
};

enum {
    SINK_INPUT_MESSAGE_POST_DATA = PA_SINK_INPUT_MESSAGE_MAX, /* data from main loop to sink input */
    SINK_INPUT_MESSAGE_DISABLE_PREBUF
};

enum {
    CONNECTION_MESSAGE_REQUEST_DATA,
    CONNECTION_MESSAGE_POST_DATA,
    CONNECTION_MESSAGE_UNLINK_CONNECTION
};

typedef struct proto_handler {
    size_t data_length;
    int (*proc)(connection *c, esd_proto_t request, const void *data, size_t length);
    const char *description;
} esd_proto_handler_info_t;

static int sink_input_pop_cb(pa_sink_input *i, size_t length, pa_memchunk *chunk);
static void sink_input_process_rewind_cb(pa_sink_input *i, size_t nbytes);
static void sink_input_update_max_rewind_cb(pa_sink_input *i, size_t nbytes);
static void sink_input_kill_cb(pa_sink_input *i);
static int sink_input_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk);
static pa_usec_t source_output_get_latency_cb(pa_source_output *o);

static void source_output_push_cb(pa_source_output *o, const pa_memchunk *chunk);
static void source_output_kill_cb(pa_source_output *o);

static int esd_proto_connect(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_stream_play(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_stream_record(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_get_latency(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_server_info(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_all_info(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_stream_pan(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_sample_pan(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_sample_cache(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_sample_free_or_play(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_sample_get_id(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_standby_or_resume(connection *c, esd_proto_t request, const void *data, size_t length);
static int esd_proto_standby_mode(connection *c, esd_proto_t request, const void *data, size_t length);

/* the big map of protocol handler info */
static struct proto_handler proto_map[ESD_PROTO_MAX] = {
    { ESD_KEY_LEN + sizeof(int),      esd_proto_connect, "connect" },
    { ESD_KEY_LEN + sizeof(int),      NULL, "lock" },
    { ESD_KEY_LEN + sizeof(int),      NULL, "unlock" },

    { ESD_NAME_MAX + 2 * sizeof(int), esd_proto_stream_play, "stream play" },
    { ESD_NAME_MAX + 2 * sizeof(int), esd_proto_stream_record, "stream rec" },
    { ESD_NAME_MAX + 2 * sizeof(int), esd_proto_stream_record, "stream mon" },

    { ESD_NAME_MAX + 3 * sizeof(int), esd_proto_sample_cache, "sample cache" },                      /* 6 */
    { sizeof(int),                    esd_proto_sample_free_or_play, "sample free" },
    { sizeof(int),                    esd_proto_sample_free_or_play, "sample play" },                /* 8 */
    { sizeof(int),                    NULL, "sample loop" },
    { sizeof(int),                    NULL, "sample stop" },
    { (size_t) -1,                    NULL, "TODO: sample kill" },

    { ESD_KEY_LEN + sizeof(int),      esd_proto_standby_or_resume, "standby" },
    { ESD_KEY_LEN + sizeof(int),      esd_proto_standby_or_resume, "resume" },                       /* 13 */

    { ESD_NAME_MAX,                   esd_proto_sample_get_id, "sample getid" },                     /* 14 */
    { ESD_NAME_MAX + 2 * sizeof(int), NULL, "stream filter" },

    { sizeof(int),                    esd_proto_server_info, "server info" },
    { sizeof(int),                    esd_proto_all_info, "all info" },
    { (size_t) -1,                    NULL, "TODO: subscribe" },
    { (size_t) -1,                    NULL, "TODO: unsubscribe" },

    { 3 * sizeof(int),                esd_proto_stream_pan, "stream pan"},
    { 3 * sizeof(int),                esd_proto_sample_pan, "sample pan" },

    { sizeof(int),                    esd_proto_standby_mode, "standby mode" },
    { 0,                              esd_proto_get_latency, "get latency" }
};

static void connection_unlink(connection *c) {
    pa_assert(c);

    if (!c->protocol)
        return;

    if (c->options) {
        pa_esound_options_unref(c->options);
        c->options = NULL;
    }

    if (c->sink_input) {
        pa_sink_input_unlink(c->sink_input);
        pa_sink_input_unref(c->sink_input);
        c->sink_input = NULL;
    }

    if (c->source_output) {
        pa_source_output_unlink(c->source_output);
        pa_source_output_unref(c->source_output);
        c->source_output = NULL;
    }

    if (c->client) {
        pa_client_free(c->client);
        c->client = NULL;
    }

    if (c->state == ESD_STREAMING_DATA)
        c->protocol->n_player--;

    if (c->io) {
        pa_iochannel_free(c->io);
        c->io = NULL;
    }

    if (c->defer_event) {
        c->protocol->core->mainloop->defer_free(c->defer_event);
        c->defer_event = NULL;
    }

    if (c->auth_timeout_event) {
        c->protocol->core->mainloop->time_free(c->auth_timeout_event);
        c->auth_timeout_event = NULL;
    }

    pa_assert_se(pa_idxset_remove_by_data(c->protocol->connections, c, NULL) == c);
    c->protocol = NULL;
    connection_unref(c);
}

static void connection_free(pa_object *obj) {
    connection *c = CONNECTION(obj);
    pa_assert(c);

    if (c->input_memblockq)
        pa_memblockq_free(c->input_memblockq);
    if (c->output_memblockq)
        pa_memblockq_free(c->output_memblockq);

    if (c->playback.current_memblock)
        pa_memblock_unref(c->playback.current_memblock);

    pa_xfree(c->read_data);
    pa_xfree(c->write_data);

    if (c->scache.memchunk.memblock)
        pa_memblock_unref(c->scache.memchunk.memblock);
    pa_xfree(c->scache.name);

    pa_xfree(c->original_name);
    pa_xfree(c);
}

static void connection_write_prepare(connection *c, size_t length) {
    size_t t;
    pa_assert(c);

    t = c->write_data_length+length;

    if (c->write_data_alloc < t)
        c->write_data = pa_xrealloc(c->write_data, c->write_data_alloc = t);

    pa_assert(c->write_data);
}

static void connection_write(connection *c, const void *data, size_t length) {
    size_t i;
    pa_assert(c);

    c->protocol->core->mainloop->defer_enable(c->defer_event, 1);

    connection_write_prepare(c, length);

    pa_assert(c->write_data);

    i = c->write_data_length;
    c->write_data_length += length;

    memcpy((uint8_t*) c->write_data + i, data, length);
}

static void format_esd2native(int format, bool swap_bytes, pa_sample_spec *ss) {
    pa_assert(ss);

    ss->channels = (uint8_t) (((format & ESD_MASK_CHAN) == ESD_STEREO) ? 2 : 1);
    if ((format & ESD_MASK_BITS) == ESD_BITS16)
        ss->format = swap_bytes ? PA_SAMPLE_S16RE : PA_SAMPLE_S16NE;
    else
        ss->format = PA_SAMPLE_U8;
}

static int format_native2esd(pa_sample_spec *ss) {
    int format = 0;

    format = (ss->format == PA_SAMPLE_U8) ? ESD_BITS8 : ESD_BITS16;
    format |= (ss->channels >= 2) ? ESD_STEREO : ESD_MONO;

    return format;
}

#define CHECK_VALIDITY(expression, ...) do {            \
        if (PA_UNLIKELY(!(expression))) {               \
            pa_log_warn(__FILE__ ": " __VA_ARGS__);     \
            return -1;                                  \
        }                                               \
    } while(0);

/*** esound commands ***/

static int esd_proto_connect(connection *c, esd_proto_t request, const void *data, size_t length) {
    uint32_t ekey;
    int ok;

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == (ESD_KEY_LEN + sizeof(uint32_t)));

    if (!c->authorized && c->options->auth_cookie) {
        const uint8_t*key;

        if ((key = pa_auth_cookie_read(c->options->auth_cookie, ESD_KEY_LEN)))
            if (memcmp(data, key, ESD_KEY_LEN) == 0)
                c->authorized = true;
    }

    if (!c->authorized) {
        pa_log("Kicked client with invalid authentication key.");
        return -1;
    }

    if (c->auth_timeout_event) {
        c->protocol->core->mainloop->time_free(c->auth_timeout_event);
        c->auth_timeout_event = NULL;
    }

    data = (const char*)data + ESD_KEY_LEN;

    memcpy(&ekey, data, sizeof(uint32_t));
    if (ekey == ESD_ENDIAN_KEY)
        c->swap_byte_order = false;
    else if (ekey == ESD_SWAP_ENDIAN_KEY)
        c->swap_byte_order = true;
    else {
        pa_log_warn("Client sent invalid endian key");
        return -1;
    }

    pa_proplist_sets(c->client->proplist, "esound.byte_order", c->swap_byte_order ? "reverse" : "native");

    ok = 1;
    connection_write(c, &ok, sizeof(int));
    return 0;
}

static int esd_proto_stream_play(connection *c, esd_proto_t request, const void *data, size_t length) {
    char name[ESD_NAME_MAX], *utf8_name;
    int32_t format, rate;
    pa_sample_spec ss;
    size_t l;
    pa_sink *sink = NULL;
    pa_sink_input_new_data sdata;
    pa_memchunk silence;

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == (sizeof(int32_t)*2+ESD_NAME_MAX));

    memcpy(&format, data, sizeof(int32_t));
    format = PA_MAYBE_INT32_SWAP(c->swap_byte_order, format);
    data = (const char*) data + sizeof(int32_t);

    memcpy(&rate, data, sizeof(int32_t));
    rate = PA_MAYBE_INT32_SWAP(c->swap_byte_order, rate);
    data = (const char*) data + sizeof(int32_t);

    ss.rate = (uint32_t) rate;
    format_esd2native(format, c->swap_byte_order, &ss);

    CHECK_VALIDITY(pa_sample_spec_valid(&ss), "Invalid sample specification");

    if (c->options->default_sink) {
        sink = pa_namereg_get(c->protocol->core, c->options->default_sink, PA_NAMEREG_SINK);
        CHECK_VALIDITY(sink, "No such sink: %s", pa_strnull(c->options->default_sink));
    }

    pa_strlcpy(name, data, sizeof(name));

    utf8_name = pa_utf8_filter(name);
    pa_client_set_name(c->client, utf8_name);
    pa_xfree(utf8_name);

    c->original_name = pa_xstrdup(name);

    pa_assert(!c->sink_input && !c->input_memblockq);

    pa_sink_input_new_data_init(&sdata);
    sdata.driver = __FILE__;
    sdata.module = c->options->module;
    sdata.client = c->client;
    if (sink)
        pa_sink_input_new_data_set_sink(&sdata, sink, false, true);
    pa_sink_input_new_data_set_sample_spec(&sdata, &ss);

    pa_sink_input_new(&c->sink_input, c->protocol->core, &sdata);
    pa_sink_input_new_data_done(&sdata);

    CHECK_VALIDITY(c->sink_input, "Failed to create sink input.");

    l = (size_t) ((double) pa_bytes_per_second(&ss)*PLAYBACK_BUFFER_SECONDS);
    pa_sink_input_get_silence(c->sink_input, &silence);
    c->input_memblockq = pa_memblockq_new(
            "esound protocol connection input_memblockq",
            0,
            l,
            l,
            &ss,
            (size_t) -1,
            l/PLAYBACK_BUFFER_FRAGMENTS,
            0,
            &silence);
    pa_memblock_unref(silence.memblock);
    pa_iochannel_socket_set_rcvbuf(c->io, l);

    c->sink_input->parent.process_msg = sink_input_process_msg;
    c->sink_input->pop = sink_input_pop_cb;
    c->sink_input->process_rewind = sink_input_process_rewind_cb;
    c->sink_input->update_max_rewind = sink_input_update_max_rewind_cb;
    c->sink_input->kill = sink_input_kill_cb;
    c->sink_input->userdata = c;

    pa_sink_input_set_requested_latency(c->sink_input, DEFAULT_SINK_LATENCY);

    c->state = ESD_STREAMING_DATA;

    c->protocol->n_player++;

    pa_atomic_store(&c->playback.missing, (int) pa_memblockq_pop_missing(c->input_memblockq));

    pa_sink_input_put(c->sink_input);

    return 0;
}

static int esd_proto_stream_record(connection *c, esd_proto_t request, const void *data, size_t length) {
    char name[ESD_NAME_MAX], *utf8_name;
    int32_t format, rate;
    pa_source *source = NULL;
    pa_sample_spec ss;
    size_t l;
    pa_source_output_new_data sdata;

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == (sizeof(int32_t)*2+ESD_NAME_MAX));

    memcpy(&format, data, sizeof(int32_t));
    format = PA_MAYBE_INT32_SWAP(c->swap_byte_order, format);
    data = (const char*) data + sizeof(int32_t);

    memcpy(&rate, data, sizeof(int32_t));
    rate = PA_MAYBE_INT32_SWAP(c->swap_byte_order, rate);
    data = (const char*) data + sizeof(int32_t);

    ss.rate = (uint32_t) rate;
    format_esd2native(format, c->swap_byte_order, &ss);

    CHECK_VALIDITY(pa_sample_spec_valid(&ss), "Invalid sample specification.");

    if (request == ESD_PROTO_STREAM_MON) {
        pa_sink* sink;

        sink = pa_namereg_get(c->protocol->core, c->options->default_sink, PA_NAMEREG_SINK);
        CHECK_VALIDITY(sink, "No such sink: %s", pa_strnull(c->options->default_sink));

        source = sink->monitor_source;
        CHECK_VALIDITY(source, "No such source.");
    } else {
        pa_assert(request == ESD_PROTO_STREAM_REC);

        if (c->options->default_source) {
            source = pa_namereg_get(c->protocol->core, c->options->default_source, PA_NAMEREG_SOURCE);
            CHECK_VALIDITY(source, "No such source: %s", pa_strnull(c->options->default_source));
        }
    }

    pa_strlcpy(name, data, sizeof(name));

    utf8_name = pa_utf8_filter(name);
    pa_client_set_name(c->client, utf8_name);
    pa_xfree(utf8_name);

    c->original_name = pa_xstrdup(name);

    pa_assert(!c->output_memblockq && !c->source_output);

    pa_source_output_new_data_init(&sdata);
    sdata.driver = __FILE__;
    sdata.module = c->options->module;
    sdata.client = c->client;
    if (source)
        pa_source_output_new_data_set_source(&sdata, source, false, true);
    pa_source_output_new_data_set_sample_spec(&sdata, &ss);

    pa_source_output_new(&c->source_output, c->protocol->core, &sdata);
    pa_source_output_new_data_done(&sdata);

    CHECK_VALIDITY(c->source_output, "Failed to create source output.");

    l = (size_t) (pa_bytes_per_second(&ss)*RECORD_BUFFER_SECONDS);
    c->output_memblockq = pa_memblockq_new(
            "esound protocol connection output_memblockq",
            0,
            l,
            l,
            &ss,
            1,
            0,
            0,
            NULL);
    pa_iochannel_socket_set_sndbuf(c->io, l);

    c->source_output->push = source_output_push_cb;
    c->source_output->kill = source_output_kill_cb;
    c->source_output->get_latency = source_output_get_latency_cb;
    c->source_output->userdata = c;

    pa_source_output_set_requested_latency(c->source_output, DEFAULT_SOURCE_LATENCY);

    c->state = ESD_STREAMING_DATA;

    c->protocol->n_player++;

    pa_source_output_put(c->source_output);

    return 0;
}

static int esd_proto_get_latency(connection *c, esd_proto_t request, const void *data, size_t length) {
    pa_sink *sink;
    int32_t latency;

    connection_assert_ref(c);
    pa_assert(!data);
    pa_assert(length == 0);

    if (!(sink = pa_namereg_get(c->protocol->core, c->options->default_sink, PA_NAMEREG_SINK)))
        latency = 0;
    else {
        double usec = (double) pa_sink_get_requested_latency(sink);
        latency = (int) ((usec*44100)/1000000);
    }

    latency = PA_MAYBE_INT32_SWAP(c->swap_byte_order, latency);
    connection_write(c, &latency, sizeof(int32_t));

    return 0;
}

static int esd_proto_server_info(connection *c, esd_proto_t request, const void *data, size_t length) {
    int32_t rate = 44100, format = ESD_STEREO|ESD_BITS16;
    int32_t response;
    pa_sink *sink;

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == sizeof(int32_t));

    if ((sink = pa_namereg_get(c->protocol->core, c->options->default_sink, PA_NAMEREG_SINK))) {
        rate = (int32_t) sink->sample_spec.rate;
        format = format_native2esd(&sink->sample_spec);
    }

    connection_write_prepare(c, sizeof(int32_t) * 3);

    response = 0;
    connection_write(c, &response, sizeof(int32_t));
    rate = PA_MAYBE_INT32_SWAP(c->swap_byte_order, rate);
    connection_write(c, &rate, sizeof(int32_t));
    format = PA_MAYBE_INT32_SWAP(c->swap_byte_order, format);
    connection_write(c, &format, sizeof(int32_t));

    return 0;
}

static int esd_proto_all_info(connection *c, esd_proto_t request, const void *data, size_t length) {
    size_t t, k, s;
    connection *conn;
    uint32_t idx = PA_IDXSET_INVALID;
    unsigned nsamples;
    char terminator[sizeof(int32_t)*6+ESD_NAME_MAX];

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == sizeof(int32_t));

    if (esd_proto_server_info(c, request, data, length) < 0)
        return -1;

    k = sizeof(int32_t)*5+ESD_NAME_MAX;
    s = sizeof(int32_t)*6+ESD_NAME_MAX;
    nsamples = pa_idxset_size(c->protocol->core->scache);
    t = s*(nsamples+1) + k*(c->protocol->n_player+1);

    connection_write_prepare(c, t);

    memset(terminator, 0, sizeof(terminator));

    PA_IDXSET_FOREACH(conn, c->protocol->connections, idx) {
        int32_t id, format = ESD_BITS16 | ESD_STEREO, rate = 44100, lvolume = ESD_VOLUME_BASE, rvolume = ESD_VOLUME_BASE;
        char name[ESD_NAME_MAX];

        if (conn->state != ESD_STREAMING_DATA)
            continue;

        pa_assert(t >= k*2+s);

        if (conn->sink_input) {
            pa_cvolume volume;
            pa_sink_input_get_volume(conn->sink_input, &volume, true);
            rate = (int32_t) conn->sink_input->sample_spec.rate;
            lvolume = (int32_t) ((volume.values[0]*ESD_VOLUME_BASE)/PA_VOLUME_NORM);
            rvolume = (int32_t) ((volume.values[volume.channels == 2 ? 1 : 0]*ESD_VOLUME_BASE)/PA_VOLUME_NORM);
            format = format_native2esd(&conn->sink_input->sample_spec);
        }

        /* id */
        id = PA_MAYBE_INT32_SWAP(c->swap_byte_order, (int32_t) (conn->index+1));
        connection_write(c, &id, sizeof(int32_t));

        /* name */
        memset(name, 0, ESD_NAME_MAX); /* don't leak old data */
        if (conn->original_name)
            strncpy(name, conn->original_name, ESD_NAME_MAX);
        else if (conn->client && pa_proplist_gets(conn->client->proplist, PA_PROP_APPLICATION_NAME))
            strncpy(name, pa_proplist_gets(conn->client->proplist, PA_PROP_APPLICATION_NAME), ESD_NAME_MAX);
        connection_write(c, name, ESD_NAME_MAX);

        /* rate */
        rate = PA_MAYBE_INT32_SWAP(c->swap_byte_order, rate);
        connection_write(c, &rate, sizeof(int32_t));

        /* left */
        lvolume = PA_MAYBE_INT32_SWAP(c->swap_byte_order, lvolume);
        connection_write(c, &lvolume, sizeof(int32_t));

        /*right*/
        rvolume = PA_MAYBE_INT32_SWAP(c->swap_byte_order, rvolume);
        connection_write(c, &rvolume, sizeof(int32_t));

        /*format*/
        format = PA_MAYBE_INT32_SWAP(c->swap_byte_order, format);
        connection_write(c, &format, sizeof(int32_t));

        t -= k;
    }

    pa_assert(t == s*(nsamples+1)+k);
    t -= k;

    connection_write(c, terminator, k);

    if (nsamples) {
        pa_scache_entry *ce;

        idx = PA_IDXSET_INVALID;

        PA_IDXSET_FOREACH(ce, c->protocol->core->scache, idx) {
            int32_t id, rate, lvolume, rvolume, format, len;
            char name[ESD_NAME_MAX];
            pa_channel_map stereo = { .channels = 2, .map = { PA_CHANNEL_POSITION_LEFT, PA_CHANNEL_POSITION_RIGHT } };
            pa_cvolume volume;
            pa_sample_spec ss;

            pa_assert(t >= s*2);

            if (ce->volume_is_set) {
                volume = ce->volume;
                pa_cvolume_remap(&volume, &ce->channel_map, &stereo);
            } else
                pa_cvolume_reset(&volume, 2);

            if (ce->memchunk.memblock)
                ss = ce->sample_spec;
            else {
                ss.format = PA_SAMPLE_S16NE;
                ss.rate = 44100;
                ss.channels = 2;
            }

            /* id */
            id = PA_MAYBE_INT32_SWAP(c->swap_byte_order, (int) (ce->index+1));
            connection_write(c, &id, sizeof(int32_t));

            /* name */
            memset(name, 0, ESD_NAME_MAX); /* don't leak old data */
            if (strncmp(ce->name, SCACHE_PREFIX, sizeof(SCACHE_PREFIX)-1) == 0)
                strncpy(name, ce->name+sizeof(SCACHE_PREFIX)-1, ESD_NAME_MAX);
            else
                pa_snprintf(name, ESD_NAME_MAX, "native.%s", ce->name);
            connection_write(c, name, ESD_NAME_MAX);

            /* rate */
            rate = PA_MAYBE_INT32_SWAP(c->swap_byte_order, (int32_t) ss.rate);
            connection_write(c, &rate, sizeof(int32_t));

            /* left */
            lvolume = PA_MAYBE_INT32_SWAP(c->swap_byte_order, (int32_t) ((volume.values[0]*ESD_VOLUME_BASE)/PA_VOLUME_NORM));
            connection_write(c, &lvolume, sizeof(int32_t));

            /*right*/
            rvolume = PA_MAYBE_INT32_SWAP(c->swap_byte_order, (int32_t) ((volume.values[1]*ESD_VOLUME_BASE)/PA_VOLUME_NORM));
            connection_write(c, &rvolume, sizeof(int32_t));

            /*format*/
            format = PA_MAYBE_INT32_SWAP(c->swap_byte_order, format_native2esd(&ss));
            connection_write(c, &format, sizeof(int32_t));

            /*length*/
            len = PA_MAYBE_INT32_SWAP(c->swap_byte_order, (int) ce->memchunk.length);
            connection_write(c, &len, sizeof(int32_t));

            t -= s;
        }
    }

    pa_assert(t == s);

    connection_write(c, terminator, s);

    return 0;
}

static int esd_proto_stream_pan(connection *c, esd_proto_t request, const void *data, size_t length) {
    int32_t ok;
    uint32_t idx, lvolume, rvolume;
    connection *conn;

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == sizeof(int32_t)*3);

    memcpy(&idx, data, sizeof(uint32_t));
    idx = PA_MAYBE_UINT32_SWAP(c->swap_byte_order, idx) - 1;
    data = (const char*)data + sizeof(uint32_t);

    memcpy(&lvolume, data, sizeof(uint32_t));
    lvolume = PA_MAYBE_UINT32_SWAP(c->swap_byte_order, lvolume);
    data = (const char*)data + sizeof(uint32_t);

    memcpy(&rvolume, data, sizeof(uint32_t));
    rvolume = PA_MAYBE_UINT32_SWAP(c->swap_byte_order, rvolume);

    if ((conn = pa_idxset_get_by_index(c->protocol->connections, idx)) && conn->sink_input) {
        pa_cvolume volume;
        volume.values[0] = (lvolume*PA_VOLUME_NORM)/ESD_VOLUME_BASE;
        volume.values[1] = (rvolume*PA_VOLUME_NORM)/ESD_VOLUME_BASE;
        volume.channels = conn->sink_input->sample_spec.channels;

        pa_sink_input_set_volume(conn->sink_input, &volume, true, true);
        ok = 1;
    } else
        ok = 0;

    connection_write(c, &ok, sizeof(int32_t));

    return 0;
}

static int esd_proto_sample_pan(connection *c, esd_proto_t request, const void *data, size_t length) {
    int32_t ok = 0;
    uint32_t idx, lvolume, rvolume;
    pa_cvolume volume;
    pa_scache_entry *ce;

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == sizeof(int32_t)*3);

    memcpy(&idx, data, sizeof(uint32_t));
    idx = PA_MAYBE_UINT32_SWAP(c->swap_byte_order, idx) - 1;
    data = (const char*)data + sizeof(uint32_t);

    memcpy(&lvolume, data, sizeof(uint32_t));
    lvolume = PA_MAYBE_UINT32_SWAP(c->swap_byte_order, lvolume);
    data = (const char*)data + sizeof(uint32_t);

    memcpy(&rvolume, data, sizeof(uint32_t));
    rvolume = PA_MAYBE_UINT32_SWAP(c->swap_byte_order, rvolume);

    volume.values[0] = (lvolume*PA_VOLUME_NORM)/ESD_VOLUME_BASE;
    volume.values[1] = (rvolume*PA_VOLUME_NORM)/ESD_VOLUME_BASE;
    volume.channels = 2;

    if ((ce = pa_idxset_get_by_index(c->protocol->core->scache, idx))) {
        pa_channel_map stereo = { .channels = 2, .map = { PA_CHANNEL_POSITION_LEFT, PA_CHANNEL_POSITION_RIGHT } };

        pa_cvolume_remap(&volume, &stereo, &ce->channel_map);
        ce->volume = volume;
        ce->volume_is_set = true;
        ok = 1;
    }

    connection_write(c, &ok, sizeof(int32_t));

    return 0;
}

static int esd_proto_sample_cache(connection *c, esd_proto_t request, const void *data, size_t length) {
    pa_sample_spec ss;
    int32_t format, rate, sc_length;
    uint32_t idx;
    char name[ESD_NAME_MAX+sizeof(SCACHE_PREFIX)-1];

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == (ESD_NAME_MAX+3*sizeof(int32_t)));

    memcpy(&format, data, sizeof(int32_t));
    format = PA_MAYBE_INT32_SWAP(c->swap_byte_order, format);
    data = (const char*)data + sizeof(int32_t);

    memcpy(&rate, data, sizeof(int32_t));
    rate = PA_MAYBE_INT32_SWAP(c->swap_byte_order, rate);
    data = (const char*)data + sizeof(int32_t);

    ss.rate = (uint32_t) rate;
    format_esd2native(format, c->swap_byte_order, &ss);

    CHECK_VALIDITY(pa_sample_spec_valid(&ss), "Invalid sample specification.");

    memcpy(&sc_length, data, sizeof(int32_t));
    sc_length = PA_MAYBE_INT32_SWAP(c->swap_byte_order, sc_length);
    data = (const char*)data + sizeof(int32_t);

    CHECK_VALIDITY(sc_length <= MAX_CACHE_SAMPLE_SIZE, "Sample too large (%d bytes).", (int)sc_length);

    strcpy(name, SCACHE_PREFIX);
    pa_strlcpy(name+sizeof(SCACHE_PREFIX)-1, data, ESD_NAME_MAX);

    CHECK_VALIDITY(pa_utf8_valid(name), "Invalid UTF8 in sample name.");

    pa_assert(!c->scache.memchunk.memblock);
    c->scache.memchunk.memblock = pa_memblock_new(c->protocol->core->mempool, (size_t) sc_length);
    c->scache.memchunk.index = 0;
    c->scache.memchunk.length = (size_t) sc_length;
    c->scache.sample_spec = ss;
    pa_assert(!c->scache.name);
    c->scache.name = pa_xstrdup(name);

    c->state = ESD_CACHING_SAMPLE;

    pa_scache_add_item(c->protocol->core, c->scache.name, NULL, NULL, NULL, c->client->proplist, &idx);

    idx += 1;
    connection_write(c, &idx, sizeof(uint32_t));

    return 0;
}

static int esd_proto_sample_get_id(connection *c, esd_proto_t request, const void *data, size_t length) {
    int32_t ok;
    uint32_t idx;
    char name[ESD_NAME_MAX+sizeof(SCACHE_PREFIX)-1];

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == ESD_NAME_MAX);

    strcpy(name, SCACHE_PREFIX);
    pa_strlcpy(name+sizeof(SCACHE_PREFIX)-1, data, ESD_NAME_MAX);

    CHECK_VALIDITY(pa_utf8_valid(name), "Invalid UTF8 in sample name.");

    ok = -1;
    if ((idx = pa_scache_get_id_by_name(c->protocol->core, name)) != PA_IDXSET_INVALID)
        ok = (int32_t) idx + 1;

    connection_write(c, &ok, sizeof(int32_t));

    return 0;
}

static int esd_proto_sample_free_or_play(connection *c, esd_proto_t request, const void *data, size_t length) {
    int32_t ok;
    const char *name;
    uint32_t idx;

    connection_assert_ref(c);
    pa_assert(data);
    pa_assert(length == sizeof(int32_t));

    memcpy(&idx, data, sizeof(uint32_t));
    idx = PA_MAYBE_UINT32_SWAP(c->swap_byte_order, idx) - 1;

    ok = 0;

    if ((name = pa_scache_get_name_by_id(c->protocol->core, idx))) {
        if (request == ESD_PROTO_SAMPLE_PLAY) {
            pa_sink *sink;

            if ((sink = pa_namereg_get(c->protocol->core, c->options->default_sink, PA_NAMEREG_SINK)))
                if (pa_scache_play_item(c->protocol->core, name, sink, PA_VOLUME_NORM, c->client->proplist, NULL) >= 0)
                    ok = (int32_t) idx + 1;
        } else {
            pa_assert(request == ESD_PROTO_SAMPLE_FREE);

            if (pa_scache_remove_item(c->protocol->core, name) >= 0)
                ok = (int32_t) idx + 1;
        }
    }

    connection_write(c, &ok, sizeof(int32_t));

    return 0;
}

static int esd_proto_standby_or_resume(connection *c, esd_proto_t request, const void *data, size_t length) {
    int32_t ok = 1;

    connection_assert_ref(c);

    connection_write_prepare(c, sizeof(int32_t) * 2);
    connection_write(c, &ok, sizeof(int32_t));

    pa_log_debug("%s of all sinks and sources requested by client %" PRIu32 ".",
                 request == ESD_PROTO_STANDBY ? "Suspending" : "Resuming", c->client->index);

    if (request == ESD_PROTO_STANDBY) {
        ok = pa_sink_suspend_all(c->protocol->core, true, PA_SUSPEND_USER) >= 0;
        ok &= pa_source_suspend_all(c->protocol->core, true, PA_SUSPEND_USER) >= 0;
    } else {
        pa_assert(request == ESD_PROTO_RESUME);
        ok = pa_sink_suspend_all(c->protocol->core, false, PA_SUSPEND_USER) >= 0;
        ok &= pa_source_suspend_all(c->protocol->core, false, PA_SUSPEND_USER) >= 0;
    }

    connection_write(c, &ok, sizeof(int32_t));

    return 0;
}

static int esd_proto_standby_mode(connection *c, esd_proto_t request, const void *data, size_t length) {
    int32_t mode;
    pa_sink *sink;
    pa_source *source;

    connection_assert_ref(c);

    mode = ESM_RUNNING;

    if ((sink = pa_namereg_get(c->protocol->core, c->options->default_sink, PA_NAMEREG_SINK)))
        if (sink->state == PA_SINK_SUSPENDED)
            mode = ESM_ON_STANDBY;

    if ((source = pa_namereg_get(c->protocol->core, c->options->default_source, PA_NAMEREG_SOURCE)))
        if (source->state == PA_SOURCE_SUSPENDED)
            mode = ESM_ON_STANDBY;

    mode = PA_MAYBE_INT32_SWAP(c->swap_byte_order, mode);

    connection_write(c, &mode, sizeof(mode));
    return 0;
}

/*** client callbacks ***/

static void client_kill_cb(pa_client *c) {
    pa_assert(c);

    connection_unlink(CONNECTION(c->userdata));
}

/*** pa_iochannel callbacks ***/

static int do_read(connection *c) {
    connection_assert_ref(c);

/*     pa_log("READ"); */

    if (c->state == ESD_NEXT_REQUEST) {
        ssize_t r;
        pa_assert(c->read_data_length < sizeof(c->request));

        if ((r = pa_iochannel_read(c->io,
                                   ((uint8_t*) &c->request) + c->read_data_length,
                                   sizeof(c->request) - c->read_data_length)) <= 0) {

            if (r < 0 && (errno == EINTR || errno == EAGAIN))
                return 0;

            pa_log_debug("read(): %s", r < 0 ? pa_cstrerror(errno) : "EOF");
            return -1;
        }

        c->read_data_length += (size_t) r;

        if (c->read_data_length >= sizeof(c->request)) {
            struct proto_handler *handler;

            c->request = PA_MAYBE_INT32_SWAP(c->swap_byte_order, c->request);

            if (c->request < ESD_PROTO_CONNECT || c->request >= ESD_PROTO_MAX) {
                pa_log("received invalid request.");
                return -1;
            }

            handler = proto_map+c->request;

/*             pa_log("executing request #%u", c->request); */

            if (!handler->proc) {
                pa_log("received unimplemented request #%u.", c->request);
                return -1;
            }

            if (handler->data_length == 0) {
                c->read_data_length = 0;

                if (handler->proc(c, c->request, NULL, 0) < 0)
                    return -1;

            } else {
                if (c->read_data_alloc < handler->data_length)
                    c->read_data = pa_xrealloc(c->read_data, c->read_data_alloc = handler->data_length);
                pa_assert(c->read_data);

                c->state = ESD_NEEDS_REQDATA;
                c->read_data_length = 0;
            }
        }

    } else if (c->state == ESD_NEEDS_REQDATA) {
        ssize_t r;
        struct proto_handler *handler = proto_map+c->request;

        pa_assert(handler->proc);

        pa_assert(c->read_data && c->read_data_length < handler->data_length);

        if ((r = pa_iochannel_read(c->io,
                                   (uint8_t*) c->read_data + c->read_data_length,
                                   handler->data_length - c->read_data_length)) <= 0) {

            if (r < 0 && (errno == EINTR || errno == EAGAIN))
                return 0;

            pa_log_debug("read(): %s", r < 0 ? pa_cstrerror(errno) : "EOF");
            return -1;
        }

        c->read_data_length += (size_t) r;
        if (c->read_data_length >= handler->data_length) {
            size_t l = c->read_data_length;
            pa_assert(handler->proc);

            c->state = ESD_NEXT_REQUEST;
            c->read_data_length = 0;

            if (handler->proc(c, c->request, c->read_data, l) < 0)
                return -1;
        }
    } else if (c->state == ESD_CACHING_SAMPLE) {
        ssize_t r;
        void *p;

        pa_assert(c->scache.memchunk.memblock);
        pa_assert(c->scache.name);
        pa_assert(c->scache.memchunk.index < c->scache.memchunk.length);

        p = pa_memblock_acquire(c->scache.memchunk.memblock);
        r = pa_iochannel_read(c->io, (uint8_t*) p+c->scache.memchunk.index, c->scache.memchunk.length-c->scache.memchunk.index);
        pa_memblock_release(c->scache.memchunk.memblock);

        if (r <= 0) {
            if (r < 0 && (errno == EINTR || errno == EAGAIN))
                return 0;

            pa_log_debug("read(): %s", r < 0 ? pa_cstrerror(errno) : "EOF");
            return -1;
        }

        c->scache.memchunk.index += (size_t) r;
        pa_assert(c->scache.memchunk.index <= c->scache.memchunk.length);

        if (c->scache.memchunk.index == c->scache.memchunk.length) {
            uint32_t idx;

            c->scache.memchunk.index = 0;
            pa_scache_add_item(c->protocol->core, c->scache.name, &c->scache.sample_spec, NULL, &c->scache.memchunk, c->client->proplist, &idx);

            pa_memblock_unref(c->scache.memchunk.memblock);
            pa_memchunk_reset(&c->scache.memchunk);

            pa_xfree(c->scache.name);
            c->scache.name = NULL;

            c->state = ESD_NEXT_REQUEST;

            idx += 1;
            connection_write(c, &idx, sizeof(uint32_t));
        }

    } else if (c->state == ESD_STREAMING_DATA && c->sink_input) {
        pa_memchunk chunk;
        ssize_t r;
        size_t l;
        void *p;
        size_t space = 0;

        pa_assert(c->input_memblockq);

/*         pa_log("STREAMING_DATA"); */

        if ((l = (size_t) pa_atomic_load(&c->playback.missing)) <= 0)
            return 0;

        if (c->playback.current_memblock) {

            space = pa_memblock_get_length(c->playback.current_memblock) - c->playback.memblock_index;

            if (space <= 0) {
                pa_memblock_unref(c->playback.current_memblock);
                c->playback.current_memblock = NULL;
            }
        }

        if (!c->playback.current_memblock) {
            pa_assert_se(c->playback.current_memblock = pa_memblock_new(c->protocol->core->mempool, (size_t) -1));
            c->playback.memblock_index = 0;

            space = pa_memblock_get_length(c->playback.current_memblock);
        }

        if (l > space)
            l = space;

        p = pa_memblock_acquire(c->playback.current_memblock);
        r = pa_iochannel_read(c->io, (uint8_t*) p+c->playback.memblock_index, l);
        pa_memblock_release(c->playback.current_memblock);

        if (r <= 0) {

            if (r < 0 && (errno == EINTR || errno == EAGAIN))
                return 0;

            pa_log_debug("read(): %s", r < 0 ? pa_cstrerror(errno) : "EOF");
            return -1;
        }

        chunk.memblock = c->playback.current_memblock;
        chunk.index = c->playback.memblock_index;
        chunk.length = (size_t) r;

        c->playback.memblock_index += (size_t) r;

        pa_atomic_sub(&c->playback.missing, (int) r);
        pa_asyncmsgq_post(c->sink_input->sink->asyncmsgq, PA_MSGOBJECT(c->sink_input), SINK_INPUT_MESSAGE_POST_DATA, NULL, 0, &chunk, NULL);
    }

    return 0;
}

static int do_write(connection *c) {
    connection_assert_ref(c);

/*     pa_log("WRITE"); */

    if (c->write_data_length) {
        ssize_t r;

        pa_assert(c->write_data_index < c->write_data_length);
        if ((r = pa_iochannel_write(c->io, (uint8_t*) c->write_data+c->write_data_index, c->write_data_length-c->write_data_index)) < 0) {
            pa_log("write(): %s", pa_cstrerror(errno));
            return -1;
        }

        c->write_data_index += (size_t) r;
        if (c->write_data_index >= c->write_data_length)
            c->write_data_length = c->write_data_index = 0;

        return 1;

    } else if (c->state == ESD_STREAMING_DATA && c->source_output) {
        pa_memchunk chunk;
        ssize_t r;
        void *p;

        if (pa_memblockq_peek(c->output_memblockq, &chunk) < 0)
            return 0;

        pa_assert(chunk.memblock);
        pa_assert(chunk.length);

        p = pa_memblock_acquire(chunk.memblock);
        r = pa_iochannel_write(c->io, (uint8_t*) p+chunk.index, chunk.length);
        pa_memblock_release(chunk.memblock);

        pa_memblock_unref(chunk.memblock);

        if (r < 0) {
            pa_log("write(): %s", pa_cstrerror(errno));
            return -1;
        }

        pa_memblockq_drop(c->output_memblockq, (size_t) r);
        return 1;
    }

    return 0;
}

static void do_work(connection *c) {
    connection_assert_ref(c);

    c->protocol->core->mainloop->defer_enable(c->defer_event, 0);

    if (c->dead)
        return;

    if (pa_iochannel_is_readable(c->io))
        if (do_read(c) < 0)
            goto fail;

    if (c->state == ESD_STREAMING_DATA && !c->sink_input && pa_iochannel_is_hungup(c->io))
        /* In case we are in capture mode we will never call read()
         * on the socket, hence we need to detect the hangup manually
         * here, instead of simply waiting for read() to return 0. */
        goto fail;

    while (pa_iochannel_is_writable(c->io)) {
        int r = do_write(c);
        if (r < 0)
            goto fail;
        if (r == 0)
            break;
    }

    return;

fail:

    if (c->state == ESD_STREAMING_DATA && c->sink_input) {
        c->dead = true;

        pa_iochannel_free(c->io);
        c->io = NULL;

        pa_asyncmsgq_post(c->sink_input->sink->asyncmsgq, PA_MSGOBJECT(c->sink_input), SINK_INPUT_MESSAGE_DISABLE_PREBUF, NULL, 0, NULL, NULL);
    } else
        connection_unlink(c);
}

static void io_callback(pa_iochannel*io, void *userdata) {
    connection *c = CONNECTION(userdata);

    connection_assert_ref(c);
    pa_assert(io);

    do_work(c);
}

static void defer_callback(pa_mainloop_api*a, pa_defer_event *e, void *userdata) {
    connection *c = CONNECTION(userdata);

    connection_assert_ref(c);
    pa_assert(e);

    do_work(c);
}

static int connection_process_msg(pa_msgobject *o, int code, void*userdata, int64_t offset, pa_memchunk *chunk) {
    connection *c = CONNECTION(o);
    connection_assert_ref(c);

    if (!c->protocol)
        return -1;

    switch (code) {
        case CONNECTION_MESSAGE_REQUEST_DATA:
            do_work(c);
            break;

        case CONNECTION_MESSAGE_POST_DATA:
/*             pa_log("got data %u", chunk->length); */
            pa_memblockq_push_align(c->output_memblockq, chunk);
            do_work(c);
            break;

        case CONNECTION_MESSAGE_UNLINK_CONNECTION:
            connection_unlink(c);
            break;
    }

    return 0;
}

/*** sink_input callbacks ***/

/* Called from thread context */
static int sink_input_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk) {
    pa_sink_input *i = PA_SINK_INPUT(o);
    connection*c;

    pa_sink_input_assert_ref(i);
    c = CONNECTION(i->userdata);
    connection_assert_ref(c);

    switch (code) {

        case SINK_INPUT_MESSAGE_POST_DATA: {
            pa_assert(chunk);

            /* New data from the main loop */
            pa_memblockq_push_align(c->input_memblockq, chunk);

            if (pa_memblockq_is_readable(c->input_memblockq) && c->playback.underrun) {
                pa_log_debug("Requesting rewind due to end of underrun.");
                pa_sink_input_request_rewind(c->sink_input, 0, false, true, false);
            }

/*             pa_log("got data, %u", pa_memblockq_get_length(c->input_memblockq)); */

            return 0;
        }

        case SINK_INPUT_MESSAGE_DISABLE_PREBUF:
            pa_memblockq_prebuf_disable(c->input_memblockq);
            return 0;

        case PA_SINK_INPUT_MESSAGE_GET_LATENCY: {
            pa_usec_t *r = userdata;

            /* The default handler will add in the extra latency added by the resampler. */
            *r = pa_bytes_to_usec(pa_memblockq_get_length(c->input_memblockq), &c->sink_input->sample_spec);
        }
        /* Fall through. */

        default:
            return pa_sink_input_process_msg(o, code, userdata, offset, chunk);
    }
}

/* Called from thread context */
static int sink_input_pop_cb(pa_sink_input *i, size_t length, pa_memchunk *chunk) {
    connection*c;

    pa_sink_input_assert_ref(i);
    c = CONNECTION(i->userdata);
    connection_assert_ref(c);
    pa_assert(chunk);

    if (pa_memblockq_peek(c->input_memblockq, chunk) < 0) {

        c->playback.underrun = true;

        if (c->dead && pa_sink_input_safe_to_remove(i))
            pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(c), CONNECTION_MESSAGE_UNLINK_CONNECTION, NULL, 0, NULL, NULL);

        return -1;
    } else {
        size_t m;

        c->playback.underrun = false;

        chunk->length = PA_MIN(length, chunk->length);
        pa_memblockq_drop(c->input_memblockq, chunk->length);
        m = pa_memblockq_pop_missing(c->input_memblockq);

        if (m > 0)
            if (pa_atomic_add(&c->playback.missing, (int) m) <= 0)
                pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(c), CONNECTION_MESSAGE_REQUEST_DATA, NULL, 0, NULL, NULL);

        return 0;
    }
}

/* Called from thread context */
static void sink_input_process_rewind_cb(pa_sink_input *i, size_t nbytes) {
    connection *c;

    pa_sink_input_assert_ref(i);
    c = CONNECTION(i->userdata);
    connection_assert_ref(c);

    /* If we are in an underrun, then we don't rewind */
    if (i->thread_info.underrun_for > 0)
        return;

    pa_memblockq_rewind(c->input_memblockq, nbytes);
}

/* Called from thread context */
static void sink_input_update_max_rewind_cb(pa_sink_input *i, size_t nbytes) {
    connection *c;

    pa_sink_input_assert_ref(i);
    c = CONNECTION(i->userdata);
    connection_assert_ref(c);

    pa_memblockq_set_maxrewind(c->input_memblockq, nbytes);
}

static void sink_input_kill_cb(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);

    connection_unlink(CONNECTION(i->userdata));
}

/*** source_output callbacks ***/

/* Called from thread context */
static void source_output_push_cb(pa_source_output *o, const pa_memchunk *chunk) {
    connection *c;

    pa_source_output_assert_ref(o);
    c = CONNECTION(o->userdata);
    pa_assert(c);
    pa_assert(chunk);

    pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(c), CONNECTION_MESSAGE_POST_DATA, NULL, 0, chunk, NULL);
}

static void source_output_kill_cb(pa_source_output *o) {
    pa_source_output_assert_ref(o);

    connection_unlink(CONNECTION(o->userdata));
}

static pa_usec_t source_output_get_latency_cb(pa_source_output *o) {
    connection*c;

    pa_source_output_assert_ref(o);
    c = CONNECTION(o->userdata);
    pa_assert(c);

    return pa_bytes_to_usec(pa_memblockq_get_length(c->output_memblockq), &c->source_output->sample_spec);
}

/*** entry points ***/

static void auth_timeout(pa_mainloop_api *m, pa_time_event *e, const struct timeval *t, void *userdata) {
    connection *c = CONNECTION(userdata);

    pa_assert(m);
    connection_assert_ref(c);
    pa_assert(c->auth_timeout_event == e);

    if (!c->authorized)
        connection_unlink(c);
}

void pa_esound_protocol_connect(pa_esound_protocol *p, pa_iochannel *io, pa_esound_options *o) {
    connection *c;
    char pname[128];
    pa_client_new_data data;
    pa_client *client;

    pa_assert(p);
    pa_assert(io);
    pa_assert(o);

    if (pa_idxset_size(p->connections)+1 > MAX_CONNECTIONS) {
        pa_log("Warning! Too many connections (%u), dropping incoming connection.", MAX_CONNECTIONS);
        pa_iochannel_free(io);
        return;
    }

    pa_client_new_data_init(&data);
    data.module = o->module;
    data.driver = __FILE__;
    pa_iochannel_socket_peer_to_string(io, pname, sizeof(pname));
    pa_proplist_setf(data.proplist, PA_PROP_APPLICATION_NAME, "EsounD client (%s)", pname);
    pa_proplist_sets(data.proplist, "esound-protocol.peer", pname);
    client = pa_client_new(p->core, &data);
    pa_client_new_data_done(&data);

    if (!client)
        return;

    c = pa_msgobject_new(connection);
    c->parent.parent.free = connection_free;
    c->parent.process_msg = connection_process_msg;
    c->protocol = p;
    c->io = io;
    pa_iochannel_set_callback(c->io, io_callback, c);

    c->client = client;
    c->client->kill = client_kill_cb;
    c->client->userdata = c;

    c->options = pa_esound_options_ref(o);
    c->authorized = false;
    c->swap_byte_order = false;
    c->dead = false;

    c->read_data_length = 0;
    c->read_data = pa_xmalloc(c->read_data_alloc = proto_map[ESD_PROTO_CONNECT].data_length);

    c->write_data_length = c->write_data_index = c->write_data_alloc = 0;
    c->write_data = NULL;

    c->state = ESD_NEEDS_REQDATA;
    c->request = ESD_PROTO_CONNECT;

    c->sink_input = NULL;
    c->input_memblockq = NULL;

    c->source_output = NULL;
    c->output_memblockq = NULL;

    c->playback.current_memblock = NULL;
    c->playback.memblock_index = 0;
    c->playback.underrun = true;
    pa_atomic_store(&c->playback.missing, 0);

    pa_memchunk_reset(&c->scache.memchunk);
    c->scache.name = NULL;

    c->original_name = NULL;

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

    c->defer_event = p->core->mainloop->defer_new(p->core->mainloop, defer_callback, c);
    p->core->mainloop->defer_enable(c->defer_event, 0);

    pa_idxset_put(p->connections, c, &c->index);
}

void pa_esound_protocol_disconnect(pa_esound_protocol *p, pa_module *m) {
    connection *c;
    void *state = NULL;

    pa_assert(p);
    pa_assert(m);

    while ((c = pa_idxset_iterate(p->connections, &state, NULL)))
        if (c->options->module == m)
            connection_unlink(c);
}

static pa_esound_protocol* esound_protocol_new(pa_core *c) {
    pa_esound_protocol *p;

    pa_assert(c);

    p = pa_xnew(pa_esound_protocol, 1);
    PA_REFCNT_INIT(p);
    p->core = c;
    p->connections = pa_idxset_new(NULL, NULL);
    p->n_player = 0;

    pa_assert_se(pa_shared_set(c, "esound-protocol", p) >= 0);

    return p;
}

pa_esound_protocol* pa_esound_protocol_get(pa_core *c) {
    pa_esound_protocol *p;

    if ((p = pa_shared_get(c, "esound-protocol")))
        return pa_esound_protocol_ref(p);

    return esound_protocol_new(c);
}

pa_esound_protocol* pa_esound_protocol_ref(pa_esound_protocol *p) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    PA_REFCNT_INC(p);

    return p;
}

void pa_esound_protocol_unref(pa_esound_protocol *p) {
    connection *c;
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    if (PA_REFCNT_DEC(p) > 0)
        return;

    while ((c = pa_idxset_first(p->connections, NULL)))
        connection_unlink(c);

    pa_idxset_free(p->connections, NULL);

    pa_assert_se(pa_shared_remove(p->core, "esound-protocol") >= 0);

    pa_xfree(p);
}

pa_esound_options* pa_esound_options_new(void) {
    pa_esound_options *o;

    o = pa_xnew0(pa_esound_options, 1);
    PA_REFCNT_INIT(o);

    return o;
}

pa_esound_options* pa_esound_options_ref(pa_esound_options *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    PA_REFCNT_INC(o);

    return o;
}

void pa_esound_options_unref(pa_esound_options *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (PA_REFCNT_DEC(o) > 0)
        return;

    if (o->auth_ip_acl)
        pa_ip_acl_free(o->auth_ip_acl);

    if (o->auth_cookie)
        pa_auth_cookie_unref(o->auth_cookie);

    pa_xfree(o->default_sink);
    pa_xfree(o->default_source);

    pa_xfree(o);
}

int pa_esound_options_parse(pa_esound_options *o, pa_core *c, pa_modargs *ma) {
    bool enabled;
    const char *acl;

    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);
    pa_assert(ma);

    if (pa_modargs_get_value_boolean(ma, "auth-anonymous", &o->auth_anonymous) < 0) {
        pa_log("auth-anonymous= expects a boolean argument.");
        return -1;
    }

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
        char *cn;

        /* The new name for this is 'auth-cookie', for compat reasons
         * we check the old name too */
        if (!(cn = pa_xstrdup(pa_modargs_get_value(ma, "auth-cookie", NULL)))) {
            if (!(cn = pa_xstrdup(pa_modargs_get_value(ma, "cookie", NULL)))) {
                if (pa_append_to_home_dir(DEFAULT_COOKIE_FILE, &cn) < 0)
                    return -1;
            }
        }

        o->auth_cookie = pa_auth_cookie_get(c, cn, true, ESD_KEY_LEN);
        pa_xfree(cn);
        if (!o->auth_cookie)
            return -1;

    } else
        o->auth_cookie = NULL;

    pa_xfree(o->default_sink);
    o->default_sink = pa_xstrdup(pa_modargs_get_value(ma, "sink", NULL));

    pa_xfree(o->default_source);
    o->default_source = pa_xstrdup(pa_modargs_get_value(ma, "source", NULL));

    return 0;
}
