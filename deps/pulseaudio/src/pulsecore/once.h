#ifndef foopulseoncehfoo
#define foopulseoncehfoo

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
#include <pulsecore/mutex.h>

typedef struct pa_once {
    pa_static_mutex mutex;
    pa_atomic_t done;
} pa_once;

#define PA_ONCE_INIT                                                    \
    {                                                                   \
        .mutex = PA_STATIC_MUTEX_INIT,                                  \
        .done = PA_ATOMIC_INIT(0)                                       \
    }

/* Not to be called directly, use the macros defined below instead */
bool pa_once_begin(pa_once *o);
void pa_once_end(pa_once *o);

#define PA_ONCE_BEGIN                                                   \
    do {                                                                \
        static pa_once _once = PA_ONCE_INIT;                            \
        if (pa_once_begin(&_once)) {{

#define PA_ONCE_END                                                     \
            }                                                           \
            pa_once_end(&_once);                                        \
        }                                                               \
    } while(0)

/*

  Usage of these macros is like this:

  void foo() {

      PA_ONCE_BEGIN {

          ... stuff to be called just once ...

      } PA_ONCE_END;
  }

*/

/* Same API but calls a function */
typedef void (*pa_once_func_t) (void);
void pa_run_once(pa_once *o, pa_once_func_t f);

#endif
