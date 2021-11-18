#ifndef foopulsememhfoo
#define foopulsememhfoo

/***
  This file is part of PulseAudio.

  Copyright 2016 Ahmed S. Darwish <darwish.07@gmail.com>

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

#include <stdbool.h>

#include <pulsecore/creds.h>
#include <pulsecore/macro.h>

typedef enum pa_mem_type {
    PA_MEM_TYPE_SHARED_POSIX,         /* Data is shared and created using POSIX shm_open() */
    PA_MEM_TYPE_SHARED_MEMFD,         /* Data is shared and created using Linux memfd_create() */
    PA_MEM_TYPE_PRIVATE,              /* Data is private and created using classic memory allocation
                                         (posix_memalign(), malloc() or anonymous mmap()) */
} pa_mem_type_t;

static inline const char *pa_mem_type_to_string(pa_mem_type_t type) {
    switch (type) {
    case PA_MEM_TYPE_SHARED_POSIX:
        return "shared posix-shm";
    case PA_MEM_TYPE_SHARED_MEMFD:
        return "shared memfd";
    case PA_MEM_TYPE_PRIVATE:
        return "private";
    }

    pa_assert_not_reached();
}

static inline bool pa_mem_type_is_shared(pa_mem_type_t t) {
    return (t == PA_MEM_TYPE_SHARED_POSIX) || (t == PA_MEM_TYPE_SHARED_MEMFD);
}

static inline bool pa_memfd_is_locally_supported() {
#if defined(HAVE_CREDS) && defined(HAVE_MEMFD)
    return true;
#else
    return false;
#endif
}

#endif
