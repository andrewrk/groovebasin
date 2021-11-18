#ifndef foopulseshmhfoo
#define foopulseshmhfoo

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
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#include <sys/types.h>

#include <pulsecore/macro.h>
#include <pulsecore/mem.h>

typedef struct pa_shm {
    pa_mem_type_t type;
    unsigned id;
    void *ptr;
    size_t size;

    /* Only for type = PA_MEM_TYPE_SHARED_POSIX */
    bool do_unlink:1;

    /* Only for type = PA_MEM_TYPE_SHARED_MEMFD
     *
     * To avoid fd leaks, we keep this fd open only until we pass it
     * to the other PA endpoint over unix domain socket.
     *
     * When we don't have ownership for the memfd fd in question (e.g.
     * pa_shm_attach()), or the file descriptor has now been closed,
     * this is set to -1.
     *
     * For the special case of a global mempool, we keep this fd
     * always open. Check comments on top of pa_mempool_new() for
     * rationale. */
    int fd;
} pa_shm;

int pa_shm_create_rw(pa_shm *m, pa_mem_type_t type, size_t size, mode_t mode);
int pa_shm_attach(pa_shm *m, pa_mem_type_t type, unsigned id, int memfd_fd, bool writable);

void pa_shm_punch(pa_shm *m, size_t offset, size_t size);

void pa_shm_free(pa_shm *m);

int pa_shm_cleanup(void);

#endif
