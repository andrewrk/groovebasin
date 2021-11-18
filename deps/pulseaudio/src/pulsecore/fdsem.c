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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif

#include <unistd.h>
#include <errno.h>

#include <pulsecore/atomic.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulse/xmalloc.h>

#ifndef HAVE_PIPE
#include <pulsecore/pipe.h>
#endif

#ifdef HAVE_SYS_EVENTFD_H
#include <sys/eventfd.h>
#endif

#include "fdsem.h"

struct pa_fdsem {
    int fds[2];
#ifdef HAVE_SYS_EVENTFD_H
    int efd;
#endif
    int write_type;
    pa_fdsem_data *data;
};

pa_fdsem *pa_fdsem_new(void) {
    pa_fdsem *f;

    f = pa_xmalloc0(PA_ALIGN(sizeof(pa_fdsem)) + PA_ALIGN(sizeof(pa_fdsem_data)));

#ifdef HAVE_SYS_EVENTFD_H
    if ((f->efd = eventfd(0, EFD_CLOEXEC)) >= 0)
        f->fds[0] = f->fds[1] = -1;
    else
#endif
    {
        if (pa_pipe_cloexec(f->fds) < 0) {
            pa_xfree(f);
            return NULL;
        }
    }

    f->data = (pa_fdsem_data*) ((uint8_t*) f + PA_ALIGN(sizeof(pa_fdsem)));

    pa_atomic_store(&f->data->waiting, 0);
    pa_atomic_store(&f->data->signalled, 0);
    pa_atomic_store(&f->data->in_pipe, 0);

    return f;
}

pa_fdsem *pa_fdsem_open_shm(pa_fdsem_data *data, int event_fd) {
    pa_fdsem *f = NULL;

    pa_assert(data);
    pa_assert(event_fd >= 0);

#ifdef HAVE_SYS_EVENTFD_H
    f = pa_xnew0(pa_fdsem, 1);

    f->efd = event_fd;
    pa_make_fd_cloexec(f->efd);
    f->fds[0] = f->fds[1] = -1;
    f->data = data;
#endif

    return f;
}

pa_fdsem *pa_fdsem_new_shm(pa_fdsem_data *data) {
    pa_fdsem *f = NULL;

    pa_assert(data);

#ifdef HAVE_SYS_EVENTFD_H

    f = pa_xnew0(pa_fdsem, 1);

    if ((f->efd = eventfd(0, EFD_CLOEXEC)) < 0) {
        pa_xfree(f);
        return NULL;
    }

    f->fds[0] = f->fds[1] = -1;
    f->data = data;

    pa_atomic_store(&f->data->waiting, 0);
    pa_atomic_store(&f->data->signalled, 0);
    pa_atomic_store(&f->data->in_pipe, 0);

#endif

    return f;
}

void pa_fdsem_free(pa_fdsem *f) {
    pa_assert(f);

#ifdef HAVE_SYS_EVENTFD_H
    if (f->efd >= 0)
        pa_close(f->efd);
#endif
    pa_close_pipe(f->fds);

    pa_xfree(f);
}

static void flush(pa_fdsem *f) {
    ssize_t r;
    pa_assert(f);

    if (pa_atomic_load(&f->data->in_pipe) <= 0)
        return;

    do {
        char x[10];

#ifdef HAVE_SYS_EVENTFD_H
        if (f->efd >= 0) {
            uint64_t u;

            if ((r = pa_read(f->efd, &u, sizeof(u), NULL)) != sizeof(u)) {

                if (r >= 0 || errno != EINTR) {
                    pa_log_error("Invalid read from eventfd: %s", r < 0 ? pa_cstrerror(errno) : "EOF");
                    pa_assert_not_reached();
                }

                continue;
            }
            r = (ssize_t) u;
        } else
#endif

        if ((r = pa_read(f->fds[0], &x, sizeof(x), NULL)) <= 0) {

            if (r >= 0 || errno != EINTR) {
                pa_log_error("Invalid read from pipe: %s", r < 0 ? pa_cstrerror(errno) : "EOF");
                pa_assert_not_reached();
            }

            continue;
        }

    } while (pa_atomic_sub(&f->data->in_pipe, (int) r) > (int) r);
}

void pa_fdsem_post(pa_fdsem *f) {
    pa_assert(f);

    if (pa_atomic_cmpxchg(&f->data->signalled, 0, 1)) {

        if (pa_atomic_load(&f->data->waiting)) {
            ssize_t r;
            char x = 'x';

            pa_atomic_inc(&f->data->in_pipe);

            for (;;) {

#ifdef HAVE_SYS_EVENTFD_H
                if (f->efd >= 0) {
                    uint64_t u = 1;

                    if ((r = pa_write(f->efd, &u, sizeof(u), &f->write_type)) != sizeof(u)) {
                        if (r >= 0 || errno != EINTR) {
                            pa_log_error("Invalid write to eventfd: %s", r < 0 ? pa_cstrerror(errno) : "EOF");
                            pa_assert_not_reached();
                        }

                        continue;
                    }
                } else
#endif

                if ((r = pa_write(f->fds[1], &x, 1, &f->write_type)) != 1) {
                    if (r >= 0 || errno != EINTR) {
                        pa_log_error("Invalid write to pipe: %s", r < 0 ? pa_cstrerror(errno) : "EOF");
                        pa_assert_not_reached();
                    }

                    continue;
                }

                break;
            }
        }
    }
}

void pa_fdsem_wait(pa_fdsem *f) {
    pa_assert(f);

    flush(f);

    if (pa_atomic_cmpxchg(&f->data->signalled, 1, 0))
        return;

    pa_atomic_inc(&f->data->waiting);

    while (!pa_atomic_cmpxchg(&f->data->signalled, 1, 0)) {
        char x[10];
        ssize_t r;

#ifdef HAVE_SYS_EVENTFD_H
        if (f->efd >= 0) {
            uint64_t u;

            if ((r = pa_read(f->efd, &u, sizeof(u), NULL)) != sizeof(u)) {

                if (r >= 0 || errno != EINTR) {
                    pa_log_error("Invalid read from eventfd: %s", r < 0 ? pa_cstrerror(errno) : "EOF");
                    pa_assert_not_reached();
                }

                continue;
            }

            r = (ssize_t) u;
        } else
#endif

        if ((r = pa_read(f->fds[0], &x, sizeof(x), NULL)) <= 0) {

            if (r >= 0 || errno != EINTR) {
                pa_log_error("Invalid read from pipe: %s", r < 0 ? pa_cstrerror(errno) : "EOF");
                pa_assert_not_reached();
            }

            continue;
        }

        pa_atomic_sub(&f->data->in_pipe, (int) r);
    }

    pa_assert_se(pa_atomic_dec(&f->data->waiting) >= 1);
}

int pa_fdsem_try(pa_fdsem *f) {
    pa_assert(f);

    flush(f);

    if (pa_atomic_cmpxchg(&f->data->signalled, 1, 0))
        return 1;

    return 0;
}

int pa_fdsem_get(pa_fdsem *f) {
    pa_assert(f);

#ifdef HAVE_SYS_EVENTFD_H
    if (f->efd >= 0)
        return f->efd;
#endif

    return f->fds[0];
}

int pa_fdsem_before_poll(pa_fdsem *f) {
    pa_assert(f);

    flush(f);

    if (pa_atomic_cmpxchg(&f->data->signalled, 1, 0))
        return -1;

    pa_atomic_inc(&f->data->waiting);

    if (pa_atomic_cmpxchg(&f->data->signalled, 1, 0)) {
        pa_assert_se(pa_atomic_dec(&f->data->waiting) >= 1);
        return -1;
    }
    return 0;
}

int pa_fdsem_after_poll(pa_fdsem *f) {
    pa_assert(f);

    pa_assert_se(pa_atomic_dec(&f->data->waiting) >= 1);

    flush(f);

    if (pa_atomic_cmpxchg(&f->data->signalled, 1, 0))
        return 1;

    return 0;
}
