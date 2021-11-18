/***
  This file is part of PulseAudio.

  Copyright 2006-2008 Lennart Poettering

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

#include <pulse/xmalloc.h>

#include <pulsecore/sink-input.h>
#include <pulsecore/thread-mq.h>

#include "play-memblockq.h"

typedef struct memblockq_stream {
    pa_msgobject parent;
    pa_core *core;
    pa_sink_input *sink_input;
    pa_memblockq *memblockq;
} memblockq_stream;

enum {
    MEMBLOCKQ_STREAM_MESSAGE_UNLINK,
};

PA_DEFINE_PRIVATE_CLASS(memblockq_stream, pa_msgobject);
#define MEMBLOCKQ_STREAM(o) (memblockq_stream_cast(o))

static void memblockq_stream_unlink(memblockq_stream *u) {
    pa_assert(u);

    if (!u->sink_input)
        return;

    pa_sink_input_unlink(u->sink_input);
    pa_sink_input_unref(u->sink_input);
    u->sink_input = NULL;

    memblockq_stream_unref(u);
}

static void memblockq_stream_free(pa_object *o) {
    memblockq_stream *u = MEMBLOCKQ_STREAM(o);
    pa_assert(u);

    if (u->memblockq)
        pa_memblockq_free(u->memblockq);

    pa_xfree(u);
}

static int memblockq_stream_process_msg(pa_msgobject *o, int code, void*userdata, int64_t offset, pa_memchunk *chunk) {
    memblockq_stream *u = MEMBLOCKQ_STREAM(o);
    memblockq_stream_assert_ref(u);

    switch (code) {
        case MEMBLOCKQ_STREAM_MESSAGE_UNLINK:
            memblockq_stream_unlink(u);
            break;
    }

    return 0;
}

static void sink_input_kill_cb(pa_sink_input *i) {
    memblockq_stream *u;

    pa_sink_input_assert_ref(i);
    u = MEMBLOCKQ_STREAM(i->userdata);
    memblockq_stream_assert_ref(u);

    memblockq_stream_unlink(u);
}

/* Called from IO thread context */
static void sink_input_state_change_cb(pa_sink_input *i, pa_sink_input_state_t state) {
    memblockq_stream *u;

    pa_sink_input_assert_ref(i);
    u = MEMBLOCKQ_STREAM(i->userdata);
    memblockq_stream_assert_ref(u);

    /* If we are added for the first time, ask for a rewinding so that
     * we are heard right-away. */
    if (PA_SINK_INPUT_IS_LINKED(state) &&
        i->thread_info.state == PA_SINK_INPUT_INIT && i->sink)
        pa_sink_input_request_rewind(i, 0, false, true, true);
}

static int sink_input_pop_cb(pa_sink_input *i, size_t nbytes, pa_memchunk *chunk) {
    memblockq_stream *u;

    pa_sink_input_assert_ref(i);
    pa_assert(chunk);
    u = MEMBLOCKQ_STREAM(i->userdata);
    memblockq_stream_assert_ref(u);

    if (!u->memblockq)
        return -1;

    if (pa_memblockq_peek(u->memblockq, chunk) < 0) {

        if (pa_sink_input_safe_to_remove(i)) {

            pa_memblockq_free(u->memblockq);
            u->memblockq = NULL;

            pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(u), MEMBLOCKQ_STREAM_MESSAGE_UNLINK, NULL, 0, NULL, NULL);
        }

        return -1;
    }

    /* If there's no memblock, there's going to be data in the memblockq after
     * a gap with length chunk->length. Drop the gap and peek the actual
     * data. There should always be some data coming - hence the assert. The
     * gap will occur if the memblockq is rewound beyond index 0.*/
    if (!chunk->memblock) {
        pa_memblockq_drop(u->memblockq, chunk->length);
        pa_assert_se(pa_memblockq_peek(u->memblockq, chunk) >= 0);
    }

    chunk->length = PA_MIN(chunk->length, nbytes);
    pa_memblockq_drop(u->memblockq, chunk->length);

    return 0;
}

static void sink_input_process_rewind_cb(pa_sink_input *i, size_t nbytes) {
    memblockq_stream *u;

    pa_sink_input_assert_ref(i);
    u = MEMBLOCKQ_STREAM(i->userdata);
    memblockq_stream_assert_ref(u);

    if (!u->memblockq)
        return;

    pa_memblockq_rewind(u->memblockq, nbytes);
}

static void sink_input_update_max_rewind_cb(pa_sink_input *i, size_t nbytes) {
    memblockq_stream *u;

    pa_sink_input_assert_ref(i);
    u = MEMBLOCKQ_STREAM(i->userdata);
    memblockq_stream_assert_ref(u);

    if (!u->memblockq)
        return;

    pa_memblockq_set_maxrewind(u->memblockq, nbytes);
}

pa_sink_input* pa_memblockq_sink_input_new(
        pa_sink *sink,
        const pa_sample_spec *ss,
        const pa_channel_map *map,
        pa_memblockq *q,
        pa_cvolume *volume,
        pa_proplist *p,
        pa_sink_input_flags_t flags) {

    memblockq_stream *u = NULL;
    pa_sink_input_new_data data;

    pa_assert(sink);
    pa_assert(ss);

    /* We allow creating this stream with no q set, so that it can be
     * filled in later */

    u = pa_msgobject_new(memblockq_stream);
    u->parent.parent.free = memblockq_stream_free;
    u->parent.process_msg = memblockq_stream_process_msg;
    u->core = sink->core;
    u->sink_input = NULL;
    u->memblockq = NULL;

    pa_sink_input_new_data_init(&data);
    pa_sink_input_new_data_set_sink(&data, sink, false, true);
    data.driver = __FILE__;
    pa_sink_input_new_data_set_sample_spec(&data, ss);
    pa_sink_input_new_data_set_channel_map(&data, map);
    pa_sink_input_new_data_set_volume(&data, volume);
    pa_proplist_update(data.proplist, PA_UPDATE_REPLACE, p);
    data.flags |= flags;

    pa_sink_input_new(&u->sink_input, sink->core, &data);
    pa_sink_input_new_data_done(&data);

    if (!u->sink_input)
        goto fail;

    u->sink_input->pop = sink_input_pop_cb;
    u->sink_input->process_rewind = sink_input_process_rewind_cb;
    u->sink_input->update_max_rewind = sink_input_update_max_rewind_cb;
    u->sink_input->kill = sink_input_kill_cb;
    u->sink_input->state_change = sink_input_state_change_cb;
    u->sink_input->userdata = u;

    if (q)
        pa_memblockq_sink_input_set_queue(u->sink_input, q);

    /* The reference to u is dangling here, because we want
     * to keep this stream around until it is fully played. */

    /* This sink input is not "put" yet, i.e. pa_sink_input_put() has
     * not been called! */

    return pa_sink_input_ref(u->sink_input);

fail:
    if (u)
        memblockq_stream_unref(u);

    return NULL;
}

int pa_play_memblockq(
        pa_sink *sink,
        const pa_sample_spec *ss,
        const pa_channel_map *map,
        pa_memblockq *q,
        pa_cvolume *volume,
        pa_proplist *p,
        pa_sink_input_flags_t flags,
        uint32_t *sink_input_index) {

    pa_sink_input *i;

    pa_assert(sink);
    pa_assert(ss);
    pa_assert(q);

    if (!(i = pa_memblockq_sink_input_new(sink, ss, map, q, volume, p, flags)))
        return -1;

    pa_sink_input_put(i);

    if (sink_input_index)
        *sink_input_index = i->index;

    pa_sink_input_unref(i);

    return 0;
}

void pa_memblockq_sink_input_set_queue(pa_sink_input *i, pa_memblockq *q) {
    memblockq_stream *u;

    pa_sink_input_assert_ref(i);
    u = MEMBLOCKQ_STREAM(i->userdata);
    memblockq_stream_assert_ref(u);

    if (u->memblockq)
        pa_memblockq_free(u->memblockq);

    if ((u->memblockq = q)) {
        pa_memblockq_set_prebuf(q, 0);
        pa_memblockq_set_silence(q, NULL);
        pa_memblockq_willneed(q);
    }
}
