/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering

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

#include "once.h"

/* See http://www.hpl.hp.com/research/linux/atomic_ops/example.php4 for the
 * reference algorithm used here. */

bool pa_once_begin(pa_once *control) {
    pa_mutex *m;

    pa_assert(control);

    if (pa_atomic_load(&control->done))
        return false;

    /* Caveat: We have to make sure that the once func has completed
     * before returning, even if the once func is not actually
     * executed by us. Hence the awkward locking. */

    m = pa_static_mutex_get(&control->mutex, false, false);
    pa_mutex_lock(m);

    if (pa_atomic_load(&control->done)) {
        pa_mutex_unlock(m);
        return false;
    }

    return true;
}

void pa_once_end(pa_once *control) {
    pa_mutex *m;

    pa_assert(control);

    pa_assert(!pa_atomic_load(&control->done));
    pa_atomic_store(&control->done, 1);

    m = pa_static_mutex_get(&control->mutex, false, false);
    pa_mutex_unlock(m);
}

/* Not reentrant -- how could it be? */
void pa_run_once(pa_once *control, pa_once_func_t func) {
    pa_assert(control);
    pa_assert(func);

    if (pa_once_begin(control)) {
        func();
        pa_once_end(control);
    }
}
