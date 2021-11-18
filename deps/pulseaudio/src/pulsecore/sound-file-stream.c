/***
  This file is part of PulseAudio.

  Copyright 2004-2008 Lennart Poettering

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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sndfile.h>

#include <pulse/xmalloc.h>
#include <pulse/util.h>

#include <pulsecore/core-error.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/log.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/core-util.h>
#include <pulsecore/mix.h>
#include <pulsecore/sndfile-util.h>

#include "sound-file-stream.h"

#define MEMBLOCKQ_MAXLENGTH (16*1024*1024)

typedef struct file_stream {
    pa_msgobject parent;
    pa_core *core;
    pa_sink_input *sink_input;

    SNDFILE *sndfile;
    sf_count_t (*readf_function)(SNDFILE *sndfile, void *ptr, sf_count_t frames);

    /* We need this memblockq here to easily fulfill rewind requests
     * (even beyond the file start!) */
    pa_memblockq *memblockq;
} file_stream;

enum {
    FILE_STREAM_MESSAGE_UNLINK
};

PA_DEFINE_PRIVATE_CLASS(file_stream, pa_msgobject);
#define FILE_STREAM(o) (file_stream_cast(o))

/* Called from main context */
static void file_stream_unlink(file_stream *u) {
    pa_assert(u);

    if (!u->sink_input)
        return;

    pa_sink_input_unlink(u->sink_input);
    pa_sink_input_unref(u->sink_input);
    u->sink_input = NULL;

    /* Make sure we don't decrease the ref count twice. */
    file_stream_unref(u);
}

/* Called from main context */
static void file_stream_free(pa_object *o) {
    file_stream *u = FILE_STREAM(o);
    pa_assert(u);

    if (u->memblockq)
        pa_memblockq_free(u->memblockq);

    if (u->sndfile)
        sf_close(u->sndfile);

    pa_xfree(u);
}

/* Called from main context */
static int file_stream_process_msg(pa_msgobject *o, int code, void*userdata, int64_t offset, pa_memchunk *chunk) {
    file_stream *u = FILE_STREAM(o);
    file_stream_assert_ref(u);

    switch (code) {
        case FILE_STREAM_MESSAGE_UNLINK:
            file_stream_unlink(u);
            break;
    }

    return 0;
}

/* Called from main context */
static void sink_input_kill_cb(pa_sink_input *i) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    file_stream_unlink(u);
}

/* Called from IO thread context */
static void sink_input_state_change_cb(pa_sink_input *i, pa_sink_input_state_t state) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    /* If we are added for the first time, ask for a rewinding so that
     * we are heard right-away. */
    if (PA_SINK_INPUT_IS_LINKED(state) &&
        i->thread_info.state == PA_SINK_INPUT_INIT && i->sink)
        pa_sink_input_request_rewind(i, 0, false, true, true);
}

/* Called from IO thread context */
static int sink_input_pop_cb(pa_sink_input *i, size_t length, pa_memchunk *chunk) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    pa_assert(chunk);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    if (!u->memblockq)
        return -1;

    for (;;) {
        pa_memchunk tchunk;
        size_t fs;
        void *p;
        sf_count_t n;

        if (pa_memblockq_peek(u->memblockq, chunk) >= 0) {
            chunk->length = PA_MIN(chunk->length, length);
            pa_memblockq_drop(u->memblockq, chunk->length);
            return 0;
        }

        if (!u->sndfile)
            break;

        tchunk.memblock = pa_memblock_new(i->sink->core->mempool, length);
        tchunk.index = 0;

        p = pa_memblock_acquire(tchunk.memblock);

        if (u->readf_function) {
            fs = pa_frame_size(&i->sample_spec);
            n = u->readf_function(u->sndfile, p, (sf_count_t) (length/fs));
        } else {
            fs = 1;
            n = sf_read_raw(u->sndfile, p, (sf_count_t) length);
        }

        pa_memblock_release(tchunk.memblock);

        if (n <= 0) {
            pa_memblock_unref(tchunk.memblock);

            sf_close(u->sndfile);
            u->sndfile = NULL;
            break;
        }

        tchunk.length = (size_t) n * fs;

        pa_memblockq_push(u->memblockq, &tchunk);
        pa_memblock_unref(tchunk.memblock);
    }

    if (pa_sink_input_safe_to_remove(i)) {
        pa_memblockq_free(u->memblockq);
        u->memblockq = NULL;

        pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(u), FILE_STREAM_MESSAGE_UNLINK, NULL, 0, NULL, NULL);
    }

    return -1;
}

static void sink_input_process_rewind_cb(pa_sink_input *i, size_t nbytes) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    if (!u->memblockq)
        return;

    pa_memblockq_rewind(u->memblockq, nbytes);
}

static void sink_input_update_max_rewind_cb(pa_sink_input *i, size_t nbytes) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    if (!u->memblockq)
        return;

    pa_memblockq_set_maxrewind(u->memblockq, nbytes);
}

int pa_play_file(
        pa_sink *sink,
        const char *fname,
        const pa_cvolume *volume) {

    file_stream *u = NULL;
    pa_sample_spec ss;
    pa_channel_map cm;
    pa_sink_input_new_data data;
    int fd;
    SF_INFO sfi;
    pa_memchunk silence;

    pa_assert(sink);
    pa_assert(fname);

    u = pa_msgobject_new(file_stream);
    u->parent.parent.free = file_stream_free;
    u->parent.process_msg = file_stream_process_msg;
    u->core = sink->core;
    u->sink_input = NULL;
    u->sndfile = NULL;
    u->readf_function = NULL;
    u->memblockq = NULL;

    if ((fd = pa_open_cloexec(fname, O_RDONLY, 0)) < 0) {
        pa_log("Failed to open file %s: %s", fname, pa_cstrerror(errno));
        goto fail;
    }

    /* FIXME: For now we just use posix_fadvise to avoid page faults
     * when accessing the file data. Eventually we should move the
     * file reader into the main event loop and pass the data over the
     * asyncmsgq. */

#ifdef HAVE_POSIX_FADVISE
    if (posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL) < 0) {
        pa_log_warn("POSIX_FADV_SEQUENTIAL failed: %s", pa_cstrerror(errno));
        goto fail;
    } else
        pa_log_debug("POSIX_FADV_SEQUENTIAL succeeded.");

    if (posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED) < 0) {
        pa_log_warn("POSIX_FADV_WILLNEED failed: %s", pa_cstrerror(errno));
        goto fail;
    } else
        pa_log_debug("POSIX_FADV_WILLNEED succeeded.");
#endif

    pa_zero(sfi);
    if (!(u->sndfile = sf_open_fd(fd, SFM_READ, &sfi, 1))) {
        pa_log("Failed to open file %s", fname);
        goto fail;
    }

    fd = -1;

    if (pa_sndfile_read_sample_spec(u->sndfile, &ss) < 0) {
        pa_log("Failed to determine file sample format.");
        goto fail;
    }

    if (pa_sndfile_read_channel_map(u->sndfile, &cm) < 0) {
        if (ss.channels > 2)
            pa_log_info("Failed to determine file channel map, synthesizing one.");
        pa_channel_map_init_extend(&cm, ss.channels, PA_CHANNEL_MAP_DEFAULT);
    }

    u->readf_function = pa_sndfile_readf_function(&ss);

    pa_sink_input_new_data_init(&data);
    pa_sink_input_new_data_set_sink(&data, sink, false, true);
    data.driver = __FILE__;
    pa_sink_input_new_data_set_sample_spec(&data, &ss);
    pa_sink_input_new_data_set_channel_map(&data, &cm);
    pa_sink_input_new_data_set_volume(&data, volume);
    pa_proplist_sets(data.proplist, PA_PROP_MEDIA_NAME, pa_path_get_filename(fname));
    pa_proplist_sets(data.proplist, PA_PROP_MEDIA_FILENAME, fname);
    pa_sndfile_init_proplist(u->sndfile, data.proplist);

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

    pa_sink_input_get_silence(u->sink_input, &silence);
    u->memblockq = pa_memblockq_new("sound-file-stream memblockq", 0, MEMBLOCKQ_MAXLENGTH, 0, &ss, 1, 1, 0, &silence);
    pa_memblock_unref(silence.memblock);

    pa_sink_input_put(u->sink_input);

    /* The reference to u is dangling here, because we want to keep
     * this stream around until it is fully played. */

    return 0;

fail:
    file_stream_unref(u);

    if (fd >= 0)
        pa_close(fd);

    return -1;
}
