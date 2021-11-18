/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <errno.h>

#include <pulsecore/thread.h>
#include <pulsecore/semaphore.h>
#include <pulsecore/macro.h>

#include <pulse/mainloop-api.h>

#include "thread-mq.h"

PA_STATIC_TLS_DECLARE_NO_FREE(thread_mq);

static void asyncmsgq_read_cb(pa_mainloop_api *api, pa_io_event *e, int fd, pa_io_event_flags_t events, void *userdata) {
    pa_thread_mq *q = userdata;
    pa_asyncmsgq *aq;

    pa_assert(events == PA_IO_EVENT_INPUT);

    if (pa_asyncmsgq_read_fd(q->outq) == fd)
        pa_asyncmsgq_ref(aq = q->outq);
    else if (pa_asyncmsgq_read_fd(q->inq) == fd)
        pa_asyncmsgq_ref(aq = q->inq);
    else
        pa_assert_not_reached();

    pa_asyncmsgq_read_after_poll(aq);

    for (;;) {
        pa_msgobject *object;
        int code;
        void *data;
        int64_t offset;
        pa_memchunk chunk;

        /* Check whether there is a message for us to process */
        while (pa_asyncmsgq_get(aq, &object, &code, &data, &offset, &chunk, 0) >= 0) {
            int ret;

            if (!object && code == PA_MESSAGE_SHUTDOWN) {
                pa_asyncmsgq_done(aq, 0);
                api->quit(api, 0);
                break;
            }

            ret = pa_asyncmsgq_dispatch(object, code, data, offset, &chunk);
            pa_asyncmsgq_done(aq, ret);
        }

        if (pa_asyncmsgq_read_before_poll(aq) == 0)
            break;
    }

    pa_asyncmsgq_unref(aq);
}

static void asyncmsgq_write_inq_cb(pa_mainloop_api *api, pa_io_event *e, int fd, pa_io_event_flags_t events, void *userdata) {
    pa_thread_mq *q = userdata;

    pa_assert(pa_asyncmsgq_write_fd(q->inq) == fd);
    pa_assert(events == PA_IO_EVENT_INPUT);

    pa_asyncmsgq_write_after_poll(q->inq);
    pa_asyncmsgq_write_before_poll(q->inq);
}

static void asyncmsgq_write_outq_cb(pa_mainloop_api *api, pa_io_event *e, int fd, pa_io_event_flags_t events, void *userdata) {
    pa_thread_mq *q = userdata;

    pa_assert(pa_asyncmsgq_write_fd(q->outq) == fd);
    pa_assert(events == PA_IO_EVENT_INPUT);

    pa_asyncmsgq_write_after_poll(q->outq);
    pa_asyncmsgq_write_before_poll(q->outq);
}

int pa_thread_mq_init_thread_mainloop(pa_thread_mq *q, pa_mainloop_api *main_mainloop, pa_mainloop_api *thread_mainloop) {
    pa_assert(q);
    pa_assert(main_mainloop);
    pa_assert(thread_mainloop);

    pa_zero(*q);

    q->inq = pa_asyncmsgq_new(0);
    if (!q->inq)
        goto fail;

    q->outq = pa_asyncmsgq_new(0);
    if (!q->outq)
        goto fail;

    q->main_mainloop = main_mainloop;
    q->thread_mainloop = thread_mainloop;

    pa_assert_se(pa_asyncmsgq_read_before_poll(q->outq) == 0);
    pa_asyncmsgq_write_before_poll(q->outq);
    pa_assert_se(q->read_main_event = main_mainloop->io_new(main_mainloop, pa_asyncmsgq_read_fd(q->outq), PA_IO_EVENT_INPUT, asyncmsgq_read_cb, q));
    pa_assert_se(q->write_thread_event = thread_mainloop->io_new(thread_mainloop, pa_asyncmsgq_write_fd(q->outq), PA_IO_EVENT_INPUT, asyncmsgq_write_outq_cb, q));

    pa_asyncmsgq_read_before_poll(q->inq);
    pa_asyncmsgq_write_before_poll(q->inq);
    pa_assert_se(q->read_thread_event = thread_mainloop->io_new(thread_mainloop, pa_asyncmsgq_read_fd(q->inq), PA_IO_EVENT_INPUT, asyncmsgq_read_cb, q));
    pa_assert_se(q->write_main_event = main_mainloop->io_new(main_mainloop, pa_asyncmsgq_write_fd(q->inq), PA_IO_EVENT_INPUT, asyncmsgq_write_inq_cb, q));

    return 0;

fail:
    pa_thread_mq_done(q);

    return -1;
}

int pa_thread_mq_init(pa_thread_mq *q, pa_mainloop_api *mainloop, pa_rtpoll *rtpoll) {
    pa_assert(q);
    pa_assert(mainloop);

    pa_zero(*q);

    q->main_mainloop = mainloop;
    q->thread_mainloop = NULL;

    q->inq = pa_asyncmsgq_new(0);
    if (!q->inq)
        goto fail;

    q->outq = pa_asyncmsgq_new(0);
    if (!q->outq)
        goto fail;

    pa_assert_se(pa_asyncmsgq_read_before_poll(q->outq) == 0);
    pa_assert_se(q->read_main_event = mainloop->io_new(mainloop, pa_asyncmsgq_read_fd(q->outq), PA_IO_EVENT_INPUT, asyncmsgq_read_cb, q));

    pa_asyncmsgq_write_before_poll(q->inq);
    pa_assert_se(q->write_main_event = mainloop->io_new(mainloop, pa_asyncmsgq_write_fd(q->inq), PA_IO_EVENT_INPUT, asyncmsgq_write_inq_cb, q));

    pa_rtpoll_item_new_asyncmsgq_read(rtpoll, PA_RTPOLL_EARLY, q->inq);
    pa_rtpoll_item_new_asyncmsgq_write(rtpoll, PA_RTPOLL_LATE, q->outq);

    return 0;

fail:
    pa_thread_mq_done(q);

    return -1;
}

void pa_thread_mq_done(pa_thread_mq *q) {
    pa_assert(q);

    /* Since we are called from main context we can be sure that the
     * inq is empty. However, the outq might still contain messages
     * for the main loop, which we need to dispatch (e.g. release
     * msgs, other stuff). Hence do so if we aren't currently
     * dispatching anyway. */

    if (q->outq && !pa_asyncmsgq_dispatching(q->outq)) {
        /* Flushing the asyncmsgq can cause arbitrarily callbacks to run,
           potentially causing recursion into pa_thread_mq_done again. */
        pa_asyncmsgq *z = q->outq;
        pa_asyncmsgq_ref(z);
        pa_asyncmsgq_flush(z, true);
        pa_asyncmsgq_unref(z);
    }

    if (q->main_mainloop) {
        if (q->read_main_event)
            q->main_mainloop->io_free(q->read_main_event);
        if (q->write_main_event)
            q->main_mainloop->io_free(q->write_main_event);
        q->read_main_event = q->write_main_event = NULL;
    }

    if (q->thread_mainloop) {
        if (q->read_thread_event)
            q->thread_mainloop->io_free(q->read_thread_event);
        if (q->write_thread_event)
            q->thread_mainloop->io_free(q->write_thread_event);
        q->read_thread_event = q->write_thread_event = NULL;
    }

    if (q->inq)
        pa_asyncmsgq_unref(q->inq);
    if (q->outq)
        pa_asyncmsgq_unref(q->outq);
    q->inq = q->outq = NULL;

    q->main_mainloop = NULL;
    q->thread_mainloop = NULL;
}

void pa_thread_mq_install(pa_thread_mq *q) {
    pa_assert(q);

    pa_assert(!(PA_STATIC_TLS_GET(thread_mq)));
    PA_STATIC_TLS_SET(thread_mq, q);
}

pa_thread_mq *pa_thread_mq_get(void) {
    return PA_STATIC_TLS_GET(thread_mq);
}
