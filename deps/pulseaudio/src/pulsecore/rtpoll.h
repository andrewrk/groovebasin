#ifndef foopulsertpollhfoo
#define foopulsertpollhfoo

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

#include <sys/types.h>
#include <limits.h>

#include <pulse/sample.h>
#include <pulsecore/asyncmsgq.h>
#include <pulsecore/fdsem.h>
#include <pulsecore/macro.h>

/* An implementation of a "real-time" poll loop. Basically, this is
 * yet another wrapper around poll(). However it has certain
 * advantages over pa_mainloop and suchlike:
 *
 * 1) High resolution timers are used
 *
 * 2) It allows raw access to the pollfd data to users
 *
 * 3) It allows arbitrary functions to be run before entering the
 * actual poll() and after it.
 *
 * Only a single interval timer is supported. */

typedef struct pa_rtpoll pa_rtpoll;
typedef struct pa_rtpoll_item pa_rtpoll_item;

typedef enum pa_rtpoll_priority {
    PA_RTPOLL_EARLY  = -100,          /* For very important stuff, like handling control messages */
    PA_RTPOLL_NORMAL = 0,             /* For normal stuff */
    PA_RTPOLL_LATE   = +100,          /* For housekeeping */
    PA_RTPOLL_NEVER  = INT_MAX,       /* For stuff that doesn't register any callbacks, but only fds to listen on */
} pa_rtpoll_priority_t;

pa_rtpoll *pa_rtpoll_new(void);
void pa_rtpoll_free(pa_rtpoll *p);

/* Sleep on the rtpoll until the time event, or any of the fd events
 * is triggered. Returns negative on error, positive if the loop
 * should continue to run, 0 when the loop should be terminated
 * cleanly. */
int pa_rtpoll_run(pa_rtpoll *f);

void pa_rtpoll_set_timer_absolute(pa_rtpoll *p, pa_usec_t usec);
void pa_rtpoll_set_timer_relative(pa_rtpoll *p, pa_usec_t usec);
void pa_rtpoll_set_timer_disabled(pa_rtpoll *p);

/* Return true when the elapsed timer was the reason for
 * the last pa_rtpoll_run() invocation to finish */
bool pa_rtpoll_timer_elapsed(pa_rtpoll *p);

/* A new fd wakeup item for pa_rtpoll */
pa_rtpoll_item *pa_rtpoll_item_new(pa_rtpoll *p, pa_rtpoll_priority_t prio, unsigned n_fds);
void pa_rtpoll_item_free(pa_rtpoll_item *i);

/* Please note that this pointer might change on every call and when
 * pa_rtpoll_run() is called. Hence: call this immediately before
 * using the pointer and don't save the result anywhere */
struct pollfd *pa_rtpoll_item_get_pollfd(pa_rtpoll_item *i, unsigned *n_fds);

/* Set the callback that shall be called when there's time to do some work: If the
 * callback returns a value > 0, the poll is skipped and the next
 * iteration of the loop will start immediately. */
void pa_rtpoll_item_set_work_callback(pa_rtpoll_item *i, int (*work_cb)(pa_rtpoll_item *i), void *userdata);

/* Set the callback that shall be called immediately before entering
 * the sleeping poll: If the callback returns a value > 0, the poll is
 * skipped and the next iteration of the loop will start immediately. */
void pa_rtpoll_item_set_before_callback(pa_rtpoll_item *i, int (*before_cb)(pa_rtpoll_item *i), void *userdata);

/* Set the callback that shall be called immediately after having
 * entered the sleeping poll */
void pa_rtpoll_item_set_after_callback(pa_rtpoll_item *i, void (*after_cb)(pa_rtpoll_item *i), void *userdata);

void* pa_rtpoll_item_get_work_userdata(pa_rtpoll_item *i);

pa_rtpoll_item *pa_rtpoll_item_new_fdsem(pa_rtpoll *p, pa_rtpoll_priority_t prio, pa_fdsem *s);
pa_rtpoll_item *pa_rtpoll_item_new_asyncmsgq_read(pa_rtpoll *p, pa_rtpoll_priority_t prio, pa_asyncmsgq *q);
pa_rtpoll_item *pa_rtpoll_item_new_asyncmsgq_write(pa_rtpoll *p, pa_rtpoll_priority_t prio, pa_asyncmsgq *q);

#endif
