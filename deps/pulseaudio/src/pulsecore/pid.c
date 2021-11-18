/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <signal.h>

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "pid.h"

/* Read the PID data from the file descriptor fd, and return it. If no
 * pid could be read, return 0, on failure (pid_t) -1 */
static pid_t read_pid(const char *fn, int fd) {
    ssize_t r;
    char t[20], *e;
    uint32_t pid;

    pa_assert(fn);
    pa_assert(fd >= 0);

    if ((r = pa_loop_read(fd, t, sizeof(t)-1, NULL)) < 0) {
        pa_log_warn("Failed to read PID file '%s': %s", fn, pa_cstrerror(errno));
        return (pid_t) -1;
    }

    if (r == 0)
        return (pid_t) 0;

    t[r] = 0;
    if ((e = strchr(t, '\n')))
        *e = 0;

    if (pa_atou(t, &pid) < 0) {
        pa_log_warn("Failed to parse PID file '%s'", fn);
        errno = EINVAL;
        return (pid_t) -1;
    }

    return (pid_t) pid;
}

static int open_pid_file(const char *fn, int mode) {
    int fd;

    pa_assert(fn);

    for (;;) {
        struct stat st;

        if ((fd = pa_open_cloexec(fn, mode
#ifdef O_NOFOLLOW
                       |O_NOFOLLOW
#endif
                       , S_IRUSR|S_IWUSR
             )) < 0) {
            if (mode != O_RDONLY || errno != ENOENT)
                pa_log_warn("Failed to open PID file '%s': %s", fn, pa_cstrerror(errno));
            goto fail;
        }

        /* Try to lock the file. If that fails, go without */
        if (pa_lock_fd(fd, 1) < 0)
            goto fail;

        if (fstat(fd, &st) < 0) {
            pa_log_warn("Failed to fstat() PID file '%s': %s", fn, pa_cstrerror(errno));
            goto fail;
        }

        /* Does the file still exist in the file system? When yes, we're done, otherwise restart */
        if (st.st_nlink >= 1)
            break;

        if (pa_lock_fd(fd, 0) < 0)
            goto fail;

        if (pa_close(fd) < 0) {
            pa_log_warn("Failed to close file '%s': %s", fn, pa_cstrerror(errno));
            fd = -1;
            goto fail;
        }
    }

    return fd;

fail:

    if (fd >= 0) {
        int saved_errno = errno;
        pa_lock_fd(fd, 0);
        pa_close(fd);
        errno = saved_errno;
    }

    return -1;
}

static int proc_name_ours(pid_t pid, const char *procname) {
#ifdef __linux__
    char bn[PATH_MAX];
    FILE *f;

    pa_snprintf(bn, sizeof(bn), "/proc/%lu/stat", (unsigned long) pid);

    if (!(f = pa_fopen_cloexec(bn, "r"))) {
        pa_log_info("Failed to open %s: %s", bn, pa_cstrerror(errno));
        return -1;
    } else {
        char *expected;
        bool good;
        char stored[64];

        if (!(fgets(stored, sizeof(stored), f))) {
            int saved_errno = feof(f) ? EINVAL : errno;
            pa_log_info("Failed to read from %s: %s", bn, feof(f) ? "EOF" : pa_cstrerror(errno));
            fclose(f);

            errno = saved_errno;
            return -1;
        }

        fclose(f);

        expected = pa_sprintf_malloc("%lu (%s)", (unsigned long) pid, procname);
        good = pa_startswith(stored, expected);
        pa_xfree(expected);

/*#if !defined(__OPTIMIZE__)*/
        if (!good) {
            /* libtool likes to rename our binary names ... */
            expected = pa_sprintf_malloc("%lu (lt-%s)", (unsigned long) pid, procname);
            good = pa_startswith(stored, expected);
            pa_xfree(expected);
        }
/*#endif*/

        return good;
    }
#else

    return 1;
#endif

}

/* Create a new PID file for the current process. */
int pa_pid_file_create(const char *procname) {
    int fd = -1;
    int ret = -1;
    char t[20];
    pid_t pid;
    size_t l;
    char *fn;

#ifdef OS_IS_WIN32
    HANDLE process;
#endif

    if (!(fn = pa_runtime_path("pid")))
        goto fail;

    if ((fd = open_pid_file(fn, O_CREAT|O_RDWR)) < 0)
        goto fail;

    if ((pid = read_pid(fn, fd)) == (pid_t) -1)
        pa_log_warn("Corrupt PID file, overwriting.");
    else if (pid > 0) {
        int ours = 1;

#ifdef OS_IS_WIN32
        if ((process = OpenProcess(PROCESS_QUERY_INFORMATION, false, pid)) != NULL) {
            CloseHandle(process);
#else
        if (kill(pid, 0) >= 0 || errno != ESRCH) {
#endif

            if (procname)
                if ((ours = proc_name_ours(pid, procname)) < 0) {
                    pa_log_warn("Could not check to see if pid %lu is a pulseaudio process. "
                                "Assuming it is and the daemon is already running.", (unsigned long) pid);
                    goto fail;
                }

            if (ours) {
                pa_log("Daemon already running.");
                ret = 1;
                goto fail;
            }
        }

        pa_log_warn("Stale PID file, overwriting.");
    }

    /* Overwrite the current PID file */
    if (lseek(fd, (off_t) 0, SEEK_SET) == (off_t) -1 || ftruncate(fd, (off_t) 0) < 0) {
        pa_log("Failed to truncate PID file '%s': %s", fn, pa_cstrerror(errno));
        goto fail;
    }

    pa_snprintf(t, sizeof(t), "%lu\n", (unsigned long) getpid());
    l = strlen(t);

    if (pa_loop_write(fd, t, l, NULL) != (ssize_t) l) {
        pa_log("Failed to write PID file.");
        goto fail;
    }

    ret = 0;

fail:
    if (fd >= 0) {
        pa_lock_fd(fd, 0);

        if (pa_close(fd) < 0) {
            pa_log("Failed to close PID file '%s': %s", fn, pa_cstrerror(errno));
            ret = -1;
        }
    }

    pa_xfree(fn);

    return ret;
}

/* Remove the PID file, if it is ours */
int pa_pid_file_remove(void) {
    int fd = -1;
    char *fn;
    int ret = -1;
    pid_t pid;

    if (!(fn = pa_runtime_path("pid")))
        goto fail;

    if ((fd = open_pid_file(fn, O_RDWR)) < 0) {
        pa_log_warn("Failed to open PID file '%s': %s", fn, pa_cstrerror(errno));
        goto fail;
    }

    if ((pid = read_pid(fn, fd)) == (pid_t) -1)
        goto fail;

    if (pid != getpid()) {
        pa_log("PID file '%s' not mine!", fn);
        goto fail;
    }

    if (ftruncate(fd, (off_t) 0) < 0) {
        pa_log_warn("Failed to truncate PID file '%s': %s", fn, pa_cstrerror(errno));
        goto fail;
    }

#ifdef OS_IS_WIN32
    pa_lock_fd(fd, 0);
    pa_close(fd);
    fd = -1;
#endif

    if (unlink(fn) < 0) {
        pa_log_warn("Failed to remove PID file '%s': %s", fn, pa_cstrerror(errno));
        goto fail;
    }

    ret = 0;

fail:

    if (fd >= 0) {
        pa_lock_fd(fd, 0);

        if (pa_close(fd) < 0) {
            pa_log_warn("Failed to close PID file '%s': %s", fn, pa_cstrerror(errno));
            ret = -1;
        }
    }

    pa_xfree(fn);

    return ret;
}

/* Check whether the daemon is currently running, i.e. if a PID file
 * exists and the PID therein too. Returns 0 on success, -1
 * otherwise. If pid is non-NULL and a running daemon was found,
 * return its PID therein */
int pa_pid_file_check_running(pid_t *pid, const char *procname) {
    return pa_pid_file_kill(0, pid, procname);
}

#ifndef OS_IS_WIN32

/* Kill a current running daemon. Return non-zero on success, -1
 * otherwise. If successful *pid contains the PID of the daemon
 * process. */
int pa_pid_file_kill(int sig, pid_t *pid, const char *procname) {
    int fd = -1;
    char *fn;
    int ret = -1;
    pid_t _pid;
#ifdef __linux__
    char *e = NULL;
#endif

    if (!pid)
        pid = &_pid;

    if (!(fn = pa_runtime_path("pid")))
        goto fail;

    if ((fd = open_pid_file(fn, O_RDONLY)) < 0) {

        if (errno == ENOENT)
            errno = ESRCH;

        goto fail;
    }

    if ((*pid = read_pid(fn, fd)) == (pid_t) -1)
        goto fail;

    if (procname) {
        int ours;

        if ((ours = proc_name_ours(*pid, procname)) < 0)
            goto fail;

        if (!ours) {
            errno = ESRCH;
            goto fail;
        }
    }

    ret = kill(*pid, sig);

fail:

    if (fd >= 0) {
        int saved_errno = errno;
        pa_lock_fd(fd, 0);
        pa_close(fd);
        errno = saved_errno;
    }

#ifdef __linux__
    pa_xfree(e);
#endif

    pa_xfree(fn);

    return ret;

}

#else /* OS_IS_WIN32 */

int pa_pid_file_kill(int sig, pid_t *pid, const char *exe_name) {
    return -1;
}

#endif
