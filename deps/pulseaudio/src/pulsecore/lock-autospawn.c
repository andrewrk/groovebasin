/***
  This file is part of PulseAudio.

  Copyright 2008 Lennart Poettering

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

#include <errno.h>
#include <string.h>
#include <signal.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include <pulse/gccmacro.h>
#include <pulse/xmalloc.h>

#include <pulsecore/i18n.h>
#include <pulsecore/poll.h>
#include <pulsecore/mutex.h>
#include <pulsecore/thread.h>
#include <pulsecore/core-util.h>

#include "lock-autospawn.h"

/* So, why do we have this complex code here with threads and pipes
 * and stuff? For two reasons: POSIX file locks are per-process, not
 * per-file descriptor. That means that two contexts within the same
 * process that try to create the autospawn lock might end up assuming
 * they both managed to lock the file. And then, POSIX locking
 * operations are synchronous. If two contexts run from the same event
 * loop it must be made sure that they do not block each other, but
 * that the locking operation can happen asynchronously. */

#define AUTOSPAWN_LOCK "autospawn.lock"

static pa_mutex *mutex;

static unsigned n_ref = 0;
static int lock_fd = -1;
static pa_mutex *lock_fd_mutex = NULL;
static pa_thread *thread = NULL;
static int pipe_fd[2] = { -1, -1 };

static enum {
    STATE_IDLE,
    STATE_OWNING,
    STATE_TAKEN,
    STATE_FAILED
} state = STATE_IDLE;

static void destroy_mutex(void) PA_GCC_DESTRUCTOR;

static int ref(void) {

    if (n_ref > 0) {

        pa_assert(pipe_fd[0] >= 0);
        pa_assert(pipe_fd[1] >= 0);
        pa_assert(lock_fd_mutex);

        n_ref++;

        return 0;
    }

    pa_assert(!lock_fd_mutex);
    pa_assert(state == STATE_IDLE);
    pa_assert(lock_fd < 0);
    pa_assert(!thread);
    pa_assert(pipe_fd[0] < 0);
    pa_assert(pipe_fd[1] < 0);

    if (pa_pipe_cloexec(pipe_fd) < 0)
        return -1;

    pa_make_fd_nonblock(pipe_fd[1]);
    pa_make_fd_nonblock(pipe_fd[0]);

    lock_fd_mutex = pa_mutex_new(false, false);

    n_ref = 1;
    return 0;
}

static void unref(bool after_fork) {

    pa_assert(n_ref > 0);
    pa_assert(pipe_fd[0] >= 0);
    pa_assert(pipe_fd[1] >= 0);
    pa_assert(lock_fd_mutex);

    n_ref--;

    if (n_ref > 0)
        return;

    /* Join threads only in the process the new thread was created in
     * to avoid undefined behaviour.
     * POSIX.1-2008 XSH 2.9.2 Thread IDs: "applications should only assume
     * that thread IDs are usable and unique within a single process." */
    if (thread) {
        if (after_fork)
            pa_thread_free_nojoin(thread);
	else
            pa_thread_free(thread);
        thread = NULL;
    }

    pa_mutex_lock(lock_fd_mutex);

    pa_assert(state != STATE_TAKEN);

    if (state == STATE_OWNING) {

        pa_assert(lock_fd >= 0);

        if (after_fork)
            pa_close(lock_fd);
        else {
            char *lf;

            if (!(lf = pa_runtime_path(AUTOSPAWN_LOCK)))
                pa_log_warn(_("Cannot access autospawn lock."));

            pa_unlock_lockfile(lf, lock_fd);
            pa_xfree(lf);
        }
    }

    lock_fd = -1;
    state = STATE_IDLE;

    pa_mutex_unlock(lock_fd_mutex);

    pa_mutex_free(lock_fd_mutex);
    lock_fd_mutex = NULL;

    pa_close(pipe_fd[0]);
    pa_close(pipe_fd[1]);
    pipe_fd[0] = pipe_fd[1] = -1;
}

static void ping(void) {
    ssize_t s;

    pa_assert(pipe_fd[1] >= 0);

    for (;;) {
        char x = 'x';

        if ((s = pa_write(pipe_fd[1], &x, 1, NULL)) == 1)
            break;

        pa_assert(s < 0);

        if (errno == EAGAIN)
            break;

        pa_assert(errno == EINTR);
    }
}

static void wait_for_ping(void) {
    ssize_t s;
    char x;
    struct pollfd pfd;
    int k;

    pa_assert(pipe_fd[0] >= 0);

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = pipe_fd[0];
    pfd.events = POLLIN;

    if ((k = pa_poll(&pfd, 1, -1)) != 1) {
        pa_assert(k < 0);
        pa_assert(errno == EINTR);
    } else if ((s = pa_read(pipe_fd[0], &x, 1, NULL)) != 1) {
        pa_assert(s < 0);
        pa_assert(errno == EAGAIN);
    }
}

static void empty_pipe(void) {
    char x[16];
    ssize_t s;

    pa_assert(pipe_fd[0] >= 0);

    if ((s = pa_read(pipe_fd[0], &x, sizeof(x), NULL)) < 1) {
        pa_assert(s < 0);
        pa_assert(errno == EAGAIN);
    }
}

static void thread_func(void *u) {
    int fd;
    char *lf;

#ifdef HAVE_PTHREAD
    sigset_t fullset;

    /* No signals in this thread please */
    sigfillset(&fullset);
    pthread_sigmask(SIG_BLOCK, &fullset, NULL);
#endif

    if (!(lf = pa_runtime_path(AUTOSPAWN_LOCK))) {
        pa_log_warn(_("Cannot access autospawn lock."));
        goto fail;
    }

    if ((fd = pa_lock_lockfile(lf)) < 0)
        goto fail;

    pa_mutex_lock(lock_fd_mutex);
    pa_assert(state == STATE_IDLE);
    lock_fd = fd;
    state = STATE_OWNING;
    pa_mutex_unlock(lock_fd_mutex);

    goto finish;

fail:
    pa_mutex_lock(lock_fd_mutex);
    pa_assert(state == STATE_IDLE);
    state = STATE_FAILED;
    pa_mutex_unlock(lock_fd_mutex);

finish:
    pa_xfree(lf);

    ping();
}

static int start_thread(void) {

    if (!thread)
        if (!(thread = pa_thread_new("autospawn", thread_func, NULL)))
            return -1;

    return 0;
}

static void create_mutex(void) {
    PA_ONCE_BEGIN {
        mutex = pa_mutex_new(false, false);
    } PA_ONCE_END;
}

static void destroy_mutex(void) {
    if (mutex)
        pa_mutex_free(mutex);
}

int pa_autospawn_lock_init(void) {
    int ret = -1;

    create_mutex();
    pa_mutex_lock(mutex);

    if (ref() < 0)
        ret = -1;
    else
        ret = pipe_fd[0];

    pa_mutex_unlock(mutex);

    return ret;
}

int pa_autospawn_lock_acquire(bool block) {
    int ret = -1;

    create_mutex();
    pa_mutex_lock(mutex);
    pa_assert(n_ref >= 1);

    pa_mutex_lock(lock_fd_mutex);

    for (;;) {

        empty_pipe();

        if (state == STATE_OWNING) {
            state = STATE_TAKEN;
            ret = 1;
            break;
        }

        if (state == STATE_FAILED) {
            ret = -1;
            break;
        }

        if (state == STATE_IDLE)
            if (start_thread() < 0)
                break;

        if (!block) {
            ret = 0;
            break;
        }

        pa_mutex_unlock(lock_fd_mutex);
        pa_mutex_unlock(mutex);

        wait_for_ping();

        pa_mutex_lock(mutex);
        pa_mutex_lock(lock_fd_mutex);
    }

    pa_mutex_unlock(lock_fd_mutex);

    pa_mutex_unlock(mutex);

    return ret;
}

void pa_autospawn_lock_release(void) {

    create_mutex();
    pa_mutex_lock(mutex);
    pa_assert(n_ref >= 1);

    pa_assert(state == STATE_TAKEN);
    state = STATE_OWNING;

    ping();

    pa_mutex_unlock(mutex);
}

void pa_autospawn_lock_done(bool after_fork) {

    create_mutex();
    pa_mutex_lock(mutex);
    pa_assert(n_ref >= 1);

    unref(after_fork);

    pa_mutex_unlock(mutex);
}
