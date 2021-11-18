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

#include <pulsecore/macro.h>
#include <pulsecore/atomic.h>

#include "sioman.h"

static pa_atomic_t stdio_inuse = PA_ATOMIC_INIT(0);

int pa_stdio_acquire(void) {
    return pa_atomic_cmpxchg(&stdio_inuse, 0, 1) ? 0 : -1;
}

void pa_stdio_release(void) {
    pa_assert_se(pa_atomic_cmpxchg(&stdio_inuse, 1, 0));
}
