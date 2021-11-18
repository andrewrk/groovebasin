#ifndef foopulsethreadmqhfoo
#define foopulsethreadmqhfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <pulse/mainloop-api.h>
#include <pulsecore/asyncmsgq.h>
#include <pulsecore/rtpoll.h>

/* Two way communication between a thread and a mainloop. Before the
 * thread is started a pa_thread_mq should be initialized and than
 * attached to the thread using pa_thread_mq_install(). */

typedef struct pa_thread_mq {
    pa_mainloop_api *main_mainloop;
    pa_mainloop_api *thread_mainloop;
    pa_asyncmsgq *inq, *outq;
    pa_io_event *read_main_event, *write_main_event;
    pa_io_event *read_thread_event, *write_thread_event;
} pa_thread_mq;

int pa_thread_mq_init(pa_thread_mq *q, pa_mainloop_api *mainloop, pa_rtpoll *rtpoll);
int pa_thread_mq_init_thread_mainloop(pa_thread_mq *q, pa_mainloop_api *main_mainloop, pa_mainloop_api *thread_mainloop);
void pa_thread_mq_done(pa_thread_mq *q);

/* Install the specified pa_thread_mq object for the current thread */
void pa_thread_mq_install(pa_thread_mq *q);

/* Return the pa_thread_mq object that is set for the current thread */
pa_thread_mq *pa_thread_mq_get(void);

/* Verify that we are in control context (aka 'main context'). */
#define pa_assert_ctl_context(s) \
    pa_assert(!pa_thread_mq_get())

/* Verify that we are in IO context (aka 'thread context'). */
#define pa_assert_io_context(s) \
    pa_assert(pa_thread_mq_get())

#endif
