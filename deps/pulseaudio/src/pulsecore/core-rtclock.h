#ifndef foopulsertclockhfoo
#define foopulsertclockhfoo

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

#include <pulsecore/macro.h>
#include <pulse/sample.h>

struct timeval;

/* Something like pulse/timeval.h but based on CLOCK_MONOTONIC */

struct timeval *pa_rtclock_get(struct timeval *ts);

pa_usec_t pa_rtclock_age(const struct timeval *tv);
bool pa_rtclock_hrtimer(void);
void pa_rtclock_hrtimer_enable(void);

/* timer with a resolution better than this are considered high-resolution */
#define PA_HRTIMER_THRESHOLD_USEC 10

/* bit to set in tv.tv_usec to mark that the timeval is in monotonic time */
#define PA_TIMEVAL_RTCLOCK ((time_t) (1LU << 30))

struct timeval* pa_rtclock_from_wallclock(struct timeval *tv);

#ifdef HAVE_CLOCK_GETTIME
struct timespec;

pa_usec_t pa_timespec_load(const struct timespec *ts);
struct timespec* pa_timespec_store(struct timespec *ts, pa_usec_t v);
#endif

struct timeval* pa_timeval_rtstore(struct timeval *tv, pa_usec_t v, bool rtclock);

#endif
