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
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#ifdef OS_IS_DARWIN
#include <libgen.h>
#include <sys/sysctl.h>
#endif

#include <pulse/xmalloc.h>
#include <pulse/timeval.h>

#include <pulsecore/socket.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/usergroup.h>

#include "util.h"

#if defined(HAVE_DLADDR) && defined(PA_GCC_WEAKREF)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <dlfcn.h>

static int _main() PA_GCC_WEAKREF(main);
#endif

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#ifdef HAVE_SCHED_H
#include <sched.h>

#if defined(__linux__) && !defined(SCHED_RESET_ON_FORK)
#define SCHED_RESET_ON_FORK 0x40000000
#endif
#endif

#ifdef __APPLE__
#include <mach/mach_init.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <sys/sysctl.h>
#endif

#ifdef HAVE_DBUS
#include <pulsecore/rtkit.h>
#endif

char *pa_get_user_name(char *s, size_t l) {
    const char *p;
    char *name = NULL;
#ifdef OS_IS_WIN32
    char buf[1024];
#endif

#ifdef HAVE_PWD_H
    struct passwd *r;
#endif

    pa_assert(s);
    pa_assert(l > 0);

    p = NULL;
#ifdef HAVE_GETUID
    p = getuid() == 0 ? "root" : NULL;
#endif
    if (!p) p = getenv("USER");
    if (!p) p = getenv("LOGNAME");
    if (!p) p = getenv("USERNAME");

    if (p) {
        name = pa_strlcpy(s, p, l);
    } else {
#ifdef HAVE_PWD_H

        if ((r = pa_getpwuid_malloc(getuid())) == NULL) {
            pa_snprintf(s, l, "%lu", (unsigned long) getuid());
            return s;
        }

        name = pa_strlcpy(s, r->pw_name, l);
        pa_getpwuid_free(r);

#elif defined(OS_IS_WIN32) /* HAVE_PWD_H */
        DWORD size = sizeof(buf);

        if (!GetUserName(buf, &size)) {
            errno = ENOENT;
            return NULL;
        }

        name = pa_strlcpy(s, buf, l);

#else /* HAVE_PWD_H */

        return NULL;
#endif /* HAVE_PWD_H */
    }

    return name;
}

char *pa_get_host_name(char *s, size_t l) {

    pa_assert(s);
    pa_assert(l > 0);

    if (gethostname(s, l) < 0)
        return NULL;

    s[l-1] = 0;
    return s;
}

char *pa_get_home_dir(char *s, size_t l) {
    char *e;
    char *dir;
#ifdef HAVE_PWD_H
    struct passwd *r;
#endif

    pa_assert(s);
    pa_assert(l > 0);

    if ((e = getenv("HOME"))) {
        dir = pa_strlcpy(s, e, l);
        goto finish;
    }

    if ((e = getenv("USERPROFILE"))) {
        dir = pa_strlcpy(s, e, l);
        goto finish;
    }

#ifdef HAVE_PWD_H
    errno = 0;
    if ((r = pa_getpwuid_malloc(getuid())) == NULL) {
        if (!errno)
            errno = ENOENT;

        return NULL;
    }

    dir = pa_strlcpy(s, r->pw_dir, l);

    pa_getpwuid_free(r);
#endif /* HAVE_PWD_H */

finish:
    if (!dir) {
        errno = ENOENT;
        return NULL;
    }

    if (!pa_is_path_absolute(dir)) {
        pa_log("Failed to get the home directory, not an absolute path: %s", dir);
        errno = ENOENT;
        return NULL;
    }

    return dir;
}

char *pa_get_binary_name(char *s, size_t l) {

    pa_assert(s);
    pa_assert(l > 0);

#if defined(OS_IS_WIN32)
    {
        char path[PATH_MAX];

        if (GetModuleFileName(NULL, path, PATH_MAX))
            return pa_strlcpy(s, pa_path_get_filename(path), l);
    }
#endif

#if defined(__linux__) || defined(__FreeBSD_kernel__)
    {
        char *rp;
        /* This works on Linux and Debian/kFreeBSD */

        if ((rp = pa_readlink("/proc/self/exe"))) {
            pa_strlcpy(s, pa_path_get_filename(rp), l);
            pa_xfree(rp);
            return s;
        }
    }
#endif

#ifdef __FreeBSD__
    {
        char *rp;

        if ((rp = pa_readlink("/proc/curproc/file"))) {
            pa_strlcpy(s, pa_path_get_filename(rp), l);
            pa_xfree(rp);
            return s;
        }
    }
#endif

#if defined(HAVE_DLADDR) && defined(PA_GCC_WEAKREF)
    {
        Dl_info info;
        if(_main) {
            int err = dladdr(&_main, &info);
            if (err != 0) {
                char *p = pa_realpath(info.dli_fname);
                if (p)
                    return p;
            }
        }
    }
#endif

#if defined(HAVE_SYS_PRCTL_H) && defined(PR_GET_NAME)
    {

        #ifndef TASK_COMM_LEN
        /* Actually defined in linux/sched.h */
        #define TASK_COMM_LEN 16
        #endif

        char tcomm[TASK_COMM_LEN+1];
        memset(tcomm, 0, sizeof(tcomm));

        /* This works on Linux only */
        if (prctl(PR_GET_NAME, (unsigned long) tcomm, 0, 0, 0) == 0)
            return pa_strlcpy(s, tcomm, l);

    }
#endif

#ifdef OS_IS_DARWIN
    {
        int mib[] = { CTL_KERN, KERN_PROCARGS, getpid(), 0 };
        size_t len, nmib = (sizeof(mib) / sizeof(mib[0])) - 1;
        char *buf;

        sysctl(mib, nmib, NULL, &len, NULL, 0);
        buf = (char *) pa_xmalloc(len);

        if (sysctl(mib, nmib, buf, &len, NULL, 0) == 0) {
            pa_strlcpy(s, basename(buf), l);
            pa_xfree(buf);
            return s;
        }

        pa_xfree(buf);

        /* fall thru */
    }
#endif /* OS_IS_DARWIN */

    errno = ENOENT;
    return NULL;
}

char *pa_path_get_filename(const char *p) {
    char *fn;

    if (!p)
        return NULL;

    if ((fn = strrchr(p, PA_PATH_SEP_CHAR)))
        return fn+1;

    return (char*) p;
}

char *pa_get_fqdn(char *s, size_t l) {
    char hn[256];
#ifdef HAVE_GETADDRINFO
    struct addrinfo *a = NULL, hints;
#endif

    pa_assert(s);
    pa_assert(l > 0);

    if (!pa_get_host_name(hn, sizeof(hn)))
        return NULL;

#ifdef HAVE_GETADDRINFO
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_CANONNAME;

    if (getaddrinfo(hn, NULL, &hints, &a))
        return pa_strlcpy(s, hn, l);

    if (!a->ai_canonname || !*a->ai_canonname) {
        freeaddrinfo(a);
        return pa_strlcpy(s, hn, l);
    }

    pa_strlcpy(s, a->ai_canonname, l);
    freeaddrinfo(a);
    return s;
#else
    return pa_strlcpy(s, hn, l);
#endif
}

int pa_msleep(unsigned long t) {
#ifdef OS_IS_WIN32
    Sleep(t);
    return 0;
#elif defined(HAVE_NANOSLEEP)
    struct timespec ts;

    ts.tv_sec = (time_t) (t / PA_MSEC_PER_SEC);
    ts.tv_nsec = (long) ((t % PA_MSEC_PER_SEC) * PA_NSEC_PER_MSEC);

    return nanosleep(&ts, NULL);
#else
#error "Platform lacks a sleep function."
#endif
}

#ifdef _POSIX_PRIORITY_SCHEDULING
static int set_scheduler(int rtprio) {
#ifdef HAVE_SCHED_H
    struct sched_param sp;
#ifdef HAVE_DBUS
    int r;
    long long rttime;
#ifdef RLIMIT_RTTIME
    struct rlimit rl;
#endif
    DBusError error;
    DBusConnection *bus;

    dbus_error_init(&error);
#endif

    pa_zero(sp);
    sp.sched_priority = rtprio;

#ifdef SCHED_RESET_ON_FORK
    if (pthread_setschedparam(pthread_self(), SCHED_RR|SCHED_RESET_ON_FORK, &sp) == 0) {
        pa_log_debug("SCHED_RR|SCHED_RESET_ON_FORK worked.");
        return 0;
    }
#endif

    if (pthread_setschedparam(pthread_self(), SCHED_RR, &sp) == 0) {
        pa_log_debug("SCHED_RR worked.");
        return 0;
    }
#endif  /* HAVE_SCHED_H */

#ifdef HAVE_DBUS
    /* Try to talk to RealtimeKit */

    if (!(bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error))) {
        pa_log("Failed to connect to system bus: %s", error.message);
        dbus_error_free(&error);
        errno = -EIO;
        return -1;
    }

    /* We need to disable exit on disconnect because otherwise
     * dbus_shutdown will kill us. See
     * https://bugs.freedesktop.org/show_bug.cgi?id=16924 */
    dbus_connection_set_exit_on_disconnect(bus, FALSE);

    rttime = rtkit_get_rttime_usec_max(bus);
    if (rttime >= 0) {
#ifdef RLIMIT_RTTIME
        r = getrlimit(RLIMIT_RTTIME, &rl);

        if (r >= 0 && (long long) rl.rlim_max > rttime) {
            pa_log_info("Clamping rlimit-rttime to %lld for RealtimeKit", rttime);
            rl.rlim_cur = rl.rlim_max = rttime;
            r = setrlimit(RLIMIT_RTTIME, &rl);

            if (r < 0)
                pa_log("setrlimit() failed: %s", pa_cstrerror(errno));
        }
#endif
        r = rtkit_make_realtime(bus, 0, rtprio);
        dbus_connection_close(bus);
        dbus_connection_unref(bus);

        if (r >= 0) {
            pa_log_debug("RealtimeKit worked.");
            return 0;
        }

        errno = -r;
    } else {
        dbus_connection_close(bus);
        dbus_connection_unref(bus);
        errno = -rttime;
    }

#else
    errno = 0;
#endif

    return -1;
}
#endif

/* Make the current thread a realtime thread, and acquire the highest
 * rtprio we can get that is less or equal the specified parameter. If
 * the thread is already realtime, don't do anything. */
int pa_thread_make_realtime(int rtprio) {

#if defined(OS_IS_DARWIN)
    struct thread_time_constraint_policy ttcpolicy;
    uint64_t freq = 0;
    size_t size = sizeof(freq);
    int ret;

    ret = sysctlbyname("hw.cpufrequency", &freq, &size, NULL, 0);
    if (ret < 0) {
        pa_log_info("Unable to read CPU frequency, acquisition of real-time scheduling failed.");
        return -1;
    }

    pa_log_debug("sysctl for hw.cpufrequency: %llu", freq);

    /* See http://developer.apple.com/library/mac/#documentation/Darwin/Conceptual/KernelProgramming/scheduler/scheduler.html */
    ttcpolicy.period = freq / 160;
    ttcpolicy.computation = freq / 3300;
    ttcpolicy.constraint = freq / 2200;
    ttcpolicy.preemptible = 1;

    ret = thread_policy_set(mach_thread_self(),
                            THREAD_TIME_CONSTRAINT_POLICY,
                            (thread_policy_t) &ttcpolicy,
                            THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (ret) {
        pa_log_info("Unable to set real-time thread priority (%08x).", ret);
        return -1;
    }

    pa_log_info("Successfully acquired real-time thread priority.");
    return 0;

#elif defined(_POSIX_PRIORITY_SCHEDULING)
    int p;

    if (set_scheduler(rtprio) >= 0) {
        pa_log_info("Successfully enabled SCHED_RR scheduling for thread, with priority %i.", rtprio);
        return 0;
    }

    for (p = rtprio-1; p >= 1; p--)
        if (set_scheduler(p) >= 0) {
            pa_log_info("Successfully enabled SCHED_RR scheduling for thread, with priority %i, which is lower than the requested %i.", p, rtprio);
            return 0;
        }
#elif defined(OS_IS_WIN32)
    /* Windows only allows realtime scheduling to be set on a per process basis.
     * Therefore, instead of making the thread realtime, just give it the highest non-realtime priority. */
    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
        pa_log_info("Successfully enabled THREAD_PRIORITY_TIME_CRITICAL scheduling for thread.");
        return 0;
    }

    pa_log_warn("SetThreadPriority() failed: 0x%08X", GetLastError());
    errno = EPERM;
#else
    errno = ENOTSUP;
#endif
    pa_log_info("Failed to acquire real-time scheduling: %s", pa_cstrerror(errno));
    return -1;
}
