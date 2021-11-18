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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <pulse/xmalloc.h>
#include <pulse/timeval.h>

#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/client.h>
#include <pulsecore/sample-util.h>
#include <pulsecore/namereg.h>
#include <pulsecore/log.h>
#include <pulsecore/core-error.h>
#include <pulsecore/atomic.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/core-util.h>
#include <pulsecore/shared.h>

#include "protocol-simple.h"

/* Don't allow more than this many concurrent connections */
#define MAX_CONNECTIONS 10

typedef struct connection {
    pa_msgobject parent;
    pa_simple_protocol *protocol;
    pa_simple_options *options;
    pa_iochannel *io;
    pa_sink_input *sink_input;
    pa_source_output *source_output;
    pa_client *client;
    pa_memblockq *input_memblockq, *output_memblockq;

    bool dead;

    struct {
        pa_memblock *current_memblock;
        size_t memblock_index;
        pa_atomic_t missing;
        bool underrun;
    } playback;
} connection;

PA_DEFINE_PRIVATE_CLASS(connection, pa_msgobject);
#define CONNECTION(o) (connection_cast(o))

struct pa_simple_protocol {
    PA_REFCNT_DECLARE;

    pa_core *core;
    pa_idxset *connections;
};

enum {
    SINK_INPUT_MESSAGE_POST_DATA = PA_SINK_INPUT_MESSAGE_MAX, /* data from main loop to sink input */
    SINK_INPUT_MESSAGE_DISABLE_PREBUF /* disabled prebuf, get playback started. */
};

enum {
    CONNECTION_MESSAGE_REQUEST_DATA,      /* data requested from sink input from the main loop */
    CONNECTION_MESSAGE_POST_DATA,         /* data from source output to main loop */
    CONNECTION_MESSAGE_UNLINK_CONNECTION  /* Please drop the connection now */
};

#define PLAYBACK_BUFFER_SECONDS (.5)
#define PLAYBACK_BUFFER_FRAGMENTS (10)
#define RECORD_BUFFER_SECONDS (5)
#define DEFAULT_SINK_LATENCY (300*PA_USEC_PER_MSEC)
#define DEFAULT_SOURCE_LATENCY (300*PA_USEC_PER_MSEC)

static void connection_unlink(connection *c) {
    pa_assert(c);

    if (!c->protocol)
        return;

    if (c->options) {
        pa_simple_options_unref(c->options);
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

    if (c->io) {
        pa_iochannel_free(c->io);
        c->io = NULL;
    }

    pa_idxset_remove_by_data(c->protocol->connections, c, NULL);
    c->protocol = NULL;
    connection_unref(c);
}

static void connection_free(pa_object *o) {
    connection *c = CONNECTION(o);
    pa_assert(c);

    if (c->playback.current_memblock)
        pa_memblock_unref(c->playback.current_memblock);

    if (c->input_memblockq)
        pa_memblockq_free(c->input_memblockq);
    if (c->output_memblockq)
        pa_memblockq_free(c->output_memblockq);

    pa_xfree(c);
}

static int do_read(connection *c) {
    pa_memchunk chunk;
    ssize_t r;
    size_t l;
    void *p;
    size_t space = 0;

    connection_assert_ref(c);

    if (!c->sink_input || (l = (size_t) pa_atomic_load(&c->playback.missing)) <= 0)
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
    r = pa_iochannel_read(c->io, (uint8_t*) p + c->playback.memblock_index, l);
    pa_memblock_release(c->playback.current_memblock);

    if (r <= 0) {

        if (r < 0 && (errno == EINTR || errno == EAGAIN))
            return 0;

        pa_log_debug("read(): %s", r == 0 ? "EOF" : pa_cstrerror(errno));
        return -1;
    }

    chunk.memblock = c->playback.current_memblock;
    chunk.index = c->playback.memblock_index;
    chunk.length = (size_t) r;

    c->playback.memblock_index += (size_t) r;

    pa_asyncmsgq_post(c->sink_input->sink->asyncmsgq, PA_MSGOBJECT(c->sink_input), SINK_INPUT_MESSAGE_POST_DATA, NULL, 0, &chunk, NULL);
    pa_atomic_sub(&c->playback.missing, (int) r);

    return 0;
}

static int do_write(connection *c) {
    pa_memchunk chunk;
    ssize_t r;
    void *p;

    connection_assert_ref(c);

    if (!c->source_output)
        return 0;

    if (pa_memblockq_peek(c->output_memblockq, &chunk) < 0) {
/*         pa_log("peek failed"); */
        return 0;
    }

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

static void do_work(connection *c) {
    connection_assert_ref(c);

    if (c->dead)
        return;

    if (pa_iochannel_is_readable(c->io))
        if (do_read(c) < 0)
            goto fail;

    if (!c->sink_input && pa_iochannel_is_hungup(c->io))
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

    if (c->sink_input) {

        /* If there is a sink input, we first drain what we already have read before shutting down the connection */
        c->dead = true;

        pa_iochannel_free(c->io);
        c->io = NULL;

        pa_asyncmsgq_post(c->sink_input->sink->asyncmsgq, PA_MSGOBJECT(c->sink_input), SINK_INPUT_MESSAGE_DISABLE_PREBUF, NULL, 0, NULL, NULL);
    } else
        connection_unlink(c);
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

            /* The default handler will add in the extra latency added by the resampler.*/
            *r = pa_bytes_to_usec(pa_memblockq_get_length(c->input_memblockq), &c->sink_input->sample_spec);
        }
        /* Fall through. */

        default:
            return pa_sink_input_process_msg(o, code, userdata, offset, chunk);
    }
}

/* Called from thread context */
static int sink_input_pop_cb(pa_sink_input *i, size_t length, pa_memchunk *chunk) {
    connection *c;

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

        chunk->length = PA_MIN(length, chunk->length);

        c->playback.underrun = false;

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

/* Called from main context */
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

/* Called from main context */
static void source_output_kill_cb(pa_source_output *o) {
    pa_source_output_assert_ref(o);

    connection_unlink(CONNECTION(o->userdata));
}

/* Called from main context */
static pa_usec_t source_output_get_latency_cb(pa_source_output *o) {
    connection*c;

    pa_source_output_assert_ref(o);
    c = CONNECTION(o->userdata);
    pa_assert(c);

    return pa_bytes_to_usec(pa_memblockq_get_length(c->output_memblockq), &c->source_output->sample_spec);
}

/*** client callbacks ***/

static void client_kill_cb(pa_client *client) {
    connection*c;

    pa_assert(client);
    c = CONNECTION(client->userdata);
    pa_assert(c);

    connection_unlink(c);
}

/*** pa_iochannel callbacks ***/

static void io_callback(pa_iochannel*io, void *userdata) {
    connection *c = CONNECTION(userdata);

    connection_assert_ref(c);
    pa_assert(io);

    do_work(c);
}

/*** socket_server callbacks ***/

void pa_simple_protocol_connect(pa_simple_protocol *p, pa_iochannel *io, pa_simple_options *o) {
    connection *c = NULL;
    char pname[128];
    pa_client_new_data client_data;

    pa_assert(p);
    pa_assert(io);
    pa_assert(o);

    if (pa_idxset_size(p->connections)+1 > MAX_CONNECTIONS) {
        pa_log("Warning! Too many connections (%u), dropping incoming connection.", MAX_CONNECTIONS);
        pa_iochannel_free(io);
        return;
    }

    c = pa_msgobject_new(connection);
    c->parent.parent.free = connection_free;
    c->parent.process_msg = connection_process_msg;
    c->io = io;
    pa_iochannel_set_callback(c->io, io_callback, c);

    c->sink_input = NULL;
    c->source_output = NULL;
    c->input_memblockq = c->output_memblockq = NULL;
    c->protocol = p;
    c->options = pa_simple_options_ref(o);
    c->playback.current_memblock = NULL;
    c->playback.memblock_index = 0;
    c->dead = false;
    c->playback.underrun = true;
    pa_atomic_store(&c->playback.missing, 0);

    pa_client_new_data_init(&client_data);
    client_data.module = o->module;
    client_data.driver = __FILE__;
    pa_iochannel_socket_peer_to_string(io, pname, sizeof(pname));
    pa_proplist_setf(client_data.proplist, PA_PROP_APPLICATION_NAME, "Simple client (%s)", pname);
    pa_proplist_sets(client_data.proplist, "simple-protocol.peer", pname);
    c->client = pa_client_new(p->core, &client_data);
    pa_client_new_data_done(&client_data);

    if (!c->client)
        goto fail;

    c->client->kill = client_kill_cb;
    c->client->userdata = c;

    if (o->playback) {
        pa_sink_input_new_data data;
        pa_memchunk silence;
        size_t l;
        pa_sink *sink;

        if (!(sink = pa_namereg_get(c->protocol->core, o->default_sink, PA_NAMEREG_SINK))) {
            pa_log("Failed to get sink.");
            goto fail;
        }

        pa_sink_input_new_data_init(&data);
        data.driver = __FILE__;
        data.module = o->module;
        data.client = c->client;
        pa_sink_input_new_data_set_sink(&data, sink, false, true);
        pa_proplist_update(data.proplist, PA_UPDATE_MERGE, c->client->proplist);
        pa_sink_input_new_data_set_sample_spec(&data, &o->sample_spec);

        pa_sink_input_new(&c->sink_input, p->core, &data);
        pa_sink_input_new_data_done(&data);

        if (!c->sink_input) {
            pa_log("Failed to create sink input.");
            goto fail;
        }

        c->sink_input->parent.process_msg = sink_input_process_msg;
        c->sink_input->pop = sink_input_pop_cb;
        c->sink_input->process_rewind = sink_input_process_rewind_cb;
        c->sink_input->update_max_rewind = sink_input_update_max_rewind_cb;
        c->sink_input->kill = sink_input_kill_cb;
        c->sink_input->userdata = c;

        pa_sink_input_set_requested_latency(c->sink_input, DEFAULT_SINK_LATENCY);

        l = (size_t) ((double) pa_bytes_per_second(&o->sample_spec)*PLAYBACK_BUFFER_SECONDS);
        pa_sink_input_get_silence(c->sink_input, &silence);
        c->input_memblockq = pa_memblockq_new(
                "simple protocol connection input_memblockq",
                0,
                l,
                l,
                &o->sample_spec,
                (size_t) -1,
                l/PLAYBACK_BUFFER_FRAGMENTS,
                0,
                &silence);
        pa_memblock_unref(silence.memblock);

        pa_iochannel_socket_set_rcvbuf(io, l);

        pa_atomic_store(&c->playback.missing, (int) pa_memblockq_pop_missing(c->input_memblockq));

        pa_sink_input_put(c->sink_input);
    }

    if (o->record) {
        pa_source_output_new_data data;
        size_t l;
        pa_source *source;

        if (!(source = pa_namereg_get(c->protocol->core, o->default_source, PA_NAMEREG_SOURCE))) {
            pa_log("Failed to get source.");
            goto fail;
        }

        pa_source_output_new_data_init(&data);
        data.driver = __FILE__;
        data.module = o->module;
        data.client = c->client;
        pa_source_output_new_data_set_source(&data, source, false, true);
        pa_proplist_update(data.proplist, PA_UPDATE_MERGE, c->client->proplist);
        pa_source_output_new_data_set_sample_spec(&data, &o->sample_spec);

        pa_source_output_new(&c->source_output, p->core, &data);
        pa_source_output_new_data_done(&data);

        if (!c->source_output) {
            pa_log("Failed to create source output.");
            goto fail;
        }
        c->source_output->push = source_output_push_cb;
        c->source_output->kill = source_output_kill_cb;
        c->source_output->get_latency = source_output_get_latency_cb;
        c->source_output->userdata = c;

        pa_source_output_set_requested_latency(c->source_output, DEFAULT_SOURCE_LATENCY);

        l = (size_t) (pa_bytes_per_second(&o->sample_spec)*RECORD_BUFFER_SECONDS);
        c->output_memblockq = pa_memblockq_new(
                "simple protocol connection output_memblockq",
                0,
                l,
                0,
                &o->sample_spec,
                1,
                0,
                0,
                NULL);
        pa_iochannel_socket_set_sndbuf(io, l);

        pa_source_output_put(c->source_output);
    }

    pa_idxset_put(p->connections, c, NULL);

    return;

fail:
    connection_unlink(c);
}

void pa_simple_protocol_disconnect(pa_simple_protocol *p, pa_module *m) {
    connection *c;
    void *state = NULL;

    pa_assert(p);
    pa_assert(m);

    while ((c = pa_idxset_iterate(p->connections, &state, NULL)))
        if (c->options->module == m)
            connection_unlink(c);
}

static pa_simple_protocol* simple_protocol_new(pa_core *c) {
    pa_simple_protocol *p;

    pa_assert(c);

    p = pa_xnew(pa_simple_protocol, 1);
    PA_REFCNT_INIT(p);
    p->core = c;
    p->connections = pa_idxset_new(NULL, NULL);

    pa_assert_se(pa_shared_set(c, "simple-protocol", p) >= 0);

    return p;
}

pa_simple_protocol* pa_simple_protocol_get(pa_core *c) {
    pa_simple_protocol *p;

    if ((p = pa_shared_get(c, "simple-protocol")))
        return pa_simple_protocol_ref(p);

    return simple_protocol_new(c);
}

pa_simple_protocol* pa_simple_protocol_ref(pa_simple_protocol *p) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    PA_REFCNT_INC(p);

    return p;
}

void pa_simple_protocol_unref(pa_simple_protocol *p) {
    connection *c;
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    if (PA_REFCNT_DEC(p) > 0)
        return;

    while ((c = pa_idxset_first(p->connections, NULL)))
        connection_unlink(c);

    pa_idxset_free(p->connections, NULL);

    pa_assert_se(pa_shared_remove(p->core, "simple-protocol") >= 0);

    pa_xfree(p);
}

pa_simple_options* pa_simple_options_new(void) {
    pa_simple_options *o;

    o = pa_xnew0(pa_simple_options, 1);
    PA_REFCNT_INIT(o);

    o->record = false;
    o->playback = true;

    return o;
}

pa_simple_options* pa_simple_options_ref(pa_simple_options *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    PA_REFCNT_INC(o);

    return o;
}

void pa_simple_options_unref(pa_simple_options *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (PA_REFCNT_DEC(o) > 0)
        return;

    pa_xfree(o->default_sink);
    pa_xfree(o->default_source);

    pa_xfree(o);
}

int pa_simple_options_parse(pa_simple_options *o, pa_core *c, pa_modargs *ma) {
    bool enabled;

    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);
    pa_assert(ma);

    o->sample_spec = c->default_sample_spec;
    if (pa_modargs_get_sample_spec_and_channel_map(ma, &o->sample_spec, &o->channel_map, PA_CHANNEL_MAP_DEFAULT) < 0) {
        pa_log("Failed to parse sample type specification.");
        return -1;
    }

    pa_xfree(o->default_source);
    o->default_source = pa_xstrdup(pa_modargs_get_value(ma, "source", NULL));

    pa_xfree(o->default_sink);
    o->default_sink = pa_xstrdup(pa_modargs_get_value(ma, "sink", NULL));

    enabled = o->record;
    if (pa_modargs_get_value_boolean(ma, "record", &enabled) < 0) {
        pa_log("record= expects a boolean argument.");
        return -1;
    }
    o->record = enabled;

    enabled = o->playback;
    if (pa_modargs_get_value_boolean(ma, "playback", &enabled) < 0) {
        pa_log("playback= expects a boolean argument.");
        return -1;
    }
    o->playback = enabled;

    if (!o->playback && !o->record) {
        pa_log("neither playback nor recording enabled for protocol.");
        return -1;
    }

    return 0;
}
