/***
  This file is part of PulseAudio.

  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <windows.h>

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>

#include "semaphore.h"

struct pa_semaphore {
    HANDLE sema;
};

pa_semaphore* pa_semaphore_new(unsigned value) {
    pa_semaphore *s;

    s = pa_xnew(pa_semaphore, 1);

    s->sema = CreateSemaphore(NULL, value, 32767, NULL);
    pa_assert(s->sema != NULL);

    return s;
}

void pa_semaphore_free(pa_semaphore *s) {
    pa_assert(s);
    CloseHandle(s->sema);
    pa_xfree(s);
}

void pa_semaphore_post(pa_semaphore *s) {
    pa_assert(s);
    ReleaseSemaphore(s->sema, 1, NULL);
}

void pa_semaphore_wait(pa_semaphore *s) {
    pa_assert(s);
    WaitForSingleObject(s->sema, INFINITE);
}
