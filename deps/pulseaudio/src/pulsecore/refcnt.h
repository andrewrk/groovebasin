#ifndef foopulserefcnthfoo
#define foopulserefcnthfoo

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
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#include <pulsecore/atomic.h>
#include <pulsecore/macro.h>
#include <pulsecore/log.h>

/* #define DEBUG_REF */

#define PA_REFCNT_DECLARE \
    pa_atomic_t _ref

#define PA_REFCNT_VALUE(p) \
    pa_atomic_load(&(p)->_ref)

#define PA_REFCNT_INIT_ZERO(p) \
    pa_atomic_store(&(p)->_ref, 0)

#ifndef DEBUG_REF

#define PA_REFCNT_INIT(p) \
    pa_atomic_store(&(p)->_ref, 1)

#define PA_REFCNT_INC(p) \
    pa_atomic_inc(&(p)->_ref)

#define PA_REFCNT_DEC(p) \
    (pa_atomic_dec(&(p)->_ref)-1)

#else

/* If you need to debug ref counting problems define DEBUG_REF and
 * set $PULSE_LOG_BACKTRACE=5 or suchlike in the shell when running
 * PA */

#define PA_REFCNT_INIT(p)                       \
    do {                                        \
        pa_atomic_store(&(p)->_ref, 1);         \
        pa_log("REF: Init %p", p);              \
    } while (false)

#define PA_REFCNT_INC(p)                        \
    do {                                        \
        pa_atomic_inc(&(p)->_ref);              \
        pa_log("REF: Inc %p", p);               \
    } while (false)                             \

#define PA_REFCNT_DEC(p)                        \
    ({                                          \
        int _j = (pa_atomic_dec(&(p)->_ref)-1); \
        if (_j <= 0)                            \
            pa_log("REF: Done %p", p);          \
        else                                    \
            pa_log("REF: Dec %p", p);           \
        _j;                                     \
     })

#endif

#endif
