/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

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

#include <unistd.h>

#include <pulsecore/atomic.h>
#include <pulsecore/macro.h>

#include "fork-detect.h"

int pa_detect_fork(void) {
    static pa_atomic_t pid = PA_ATOMIC_INIT((int) -1);

    /* Some really stupid applications (Hey, vim, that means you!)
     * love to fork after initializing
     * gtk/libcanberra/pulseaudio. This is really bad style. We
     * however have to deal with this cleanly, so we try to detect the
     * forks making sure all our calls fail cleanly after the fork. */

    pa_assert_cc(sizeof(pa_atomic_t) >= sizeof(pid_t));

    for (;;) {
        pid_t stored_pid = (pid_t) pa_atomic_load(&pid);

        /* First let's check whether the current pid matches the stored one */
        if (stored_pid == getpid())
            return false;

        /* Does it contain a different PID than ours? Then the process got forked. */
        if ((int) stored_pid != (int) -1)
            return true;

        /* Ok, it still contains no PID, then store it */
        if (pa_atomic_cmpxchg(&pid, (int) -1, (int) getpid()))
            return false;
    }
}
