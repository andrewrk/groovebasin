/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2004 Joe Marcus Clarke
  Copyright 2006-2007 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

#if defined(HAVE_REGEX_H)
#include <regex.h>
#elif defined(HAVE_PCREPOSIX_H)
#include <pcreposix.h>
#endif

#ifdef HAVE_STRTOD_L
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_XLOCALE_H
#include <xlocale.h>
#endif
#endif

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#ifdef HAVE_SYS_CAPABILITY_H
#include <sys/capability.h>
#endif

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#ifndef ENOTSUP
#define ENOTSUP   135
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_GRP_H
#include <grp.h>
#endif

#ifdef HAVE_LIBSAMPLERATE
#include <samplerate.h>
#endif

#ifdef HAVE_DBUS
#include <pulsecore/rtkit.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/personality.h>
#endif

#ifdef HAVE_CPUID_H
#include <cpuid.h>
#endif

#include <pulse/xmalloc.h>
#include <pulse/util.h>
#include <pulse/utf8.h>

#include <pulsecore/core-error.h>
#include <pulsecore/socket.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/thread.h>
#include <pulsecore/strbuf.h>
#include <pulsecore/usergroup.h>
#include <pulsecore/strlist.h>
#include <pulsecore/pipe.h>
#include <pulsecore/once.h>

#include "core-util.h"

/* Not all platforms have this */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define NEWLINE "\r\n"
#define WHITESPACE "\n\r \t"

static pa_strlist *recorded_env = NULL;

#ifdef OS_IS_WIN32
static fd_set nonblocking_fds;
#endif

#ifdef OS_IS_WIN32

/* Returns the directory of the current DLL, with '/bin/' removed if it is the last component */
char *pa_win32_get_toplevel(HANDLE handle) {
    static char *toplevel = NULL;

    if (!toplevel) {
        char library_path[MAX_PATH];
        char *p;

        if (!GetModuleFileName(handle, library_path, MAX_PATH))
            return NULL;

        toplevel = pa_xstrdup(library_path);

        p = strrchr(toplevel, PA_PATH_SEP_CHAR);
        if (p)
            *p = '\0';

        p = strrchr(toplevel, PA_PATH_SEP_CHAR);
        if (p && pa_streq(p + 1, "bin"))
            *p = '\0';
    }

    return toplevel;
}

#endif

static void set_nonblock(int fd, bool nonblock) {

#ifdef O_NONBLOCK
    int v, nv;
    pa_assert(fd >= 0);

    pa_assert_se((v = fcntl(fd, F_GETFL)) >= 0);

    if (nonblock)
        nv = v | O_NONBLOCK;
    else
        nv = v & ~O_NONBLOCK;

    if (v != nv)
        pa_assert_se(fcntl(fd, F_SETFL, nv) >= 0);

#elif defined(OS_IS_WIN32)
    u_long arg;

    if (nonblock)
        arg = 1;
    else
        arg = 0;

    if (ioctlsocket(fd, FIONBIO, &arg) < 0) {
        pa_assert_se(WSAGetLastError() == WSAENOTSOCK);
        pa_log_warn("Only sockets can be made non-blocking!");
        return;
    }

    /* There is no method to query status, so we remember all fds */
    if (nonblock)
        FD_SET(fd, &nonblocking_fds);
    else
        FD_CLR(fd, &nonblocking_fds);
#else
    pa_log_warn("Non-blocking I/O not supported.!");
#endif

}

/** Make a file descriptor nonblock. Doesn't do any error checking */
void pa_make_fd_nonblock(int fd) {
    set_nonblock(fd, true);
}

/** Make a file descriptor blocking. Doesn't do any error checking */
void pa_make_fd_block(int fd) {
    set_nonblock(fd, false);
}

/** Query if a file descriptor is non-blocking */
bool pa_is_fd_nonblock(int fd) {

#ifdef O_NONBLOCK
    int v;
    pa_assert(fd >= 0);

    pa_assert_se((v = fcntl(fd, F_GETFL)) >= 0);

    return !!(v & O_NONBLOCK);

#elif defined(OS_IS_WIN32)
    return !!FD_ISSET(fd, &nonblocking_fds);
#else
    return false;
#endif

}

/* Set the FD_CLOEXEC flag for a fd */
void pa_make_fd_cloexec(int fd) {

#ifdef FD_CLOEXEC
    int v;
    pa_assert(fd >= 0);

    pa_assert_se((v = fcntl(fd, F_GETFD, 0)) >= 0);

    if (!(v & FD_CLOEXEC))
        pa_assert_se(fcntl(fd, F_SETFD, v|FD_CLOEXEC) >= 0);
#endif

}

/** Creates a directory securely. Will create parent directories recursively if
 * required. This will not update permissions on parent directories if they
 * already exist, however. */
int pa_make_secure_dir(const char* dir, mode_t m, uid_t uid, gid_t gid, bool update_perms) {
    struct stat st;
    int r, saved_errno;
    bool retry = true;

    pa_assert(dir);

again:
#ifdef OS_IS_WIN32
    r = mkdir(dir);
#else
{
    mode_t u;
    u = umask((~m) & 0777);
    r = mkdir(dir, m);
    umask(u);
}
#endif

    if (r < 0 && errno == ENOENT && retry) {
        /* If a parent directory in the path doesn't exist, try to create that
         * first, then try again. */
        pa_make_secure_parent_dir(dir, m, uid, gid, false);
        retry = false;
        goto again;
    }

    if (r < 0 && errno != EEXIST)
        return -1;

#if defined(HAVE_FSTAT) && !defined(OS_IS_WIN32)
{
    int fd;
    if ((fd = open(dir,
#ifdef O_CLOEXEC
                   O_CLOEXEC|
#endif
#ifdef O_NOCTTY
                   O_NOCTTY|
#endif
#ifdef O_NOFOLLOW
                   O_NOFOLLOW|
#endif
                   O_RDONLY)) < 0)
        goto fail;

    if (fstat(fd, &st) < 0) {
        pa_assert_se(pa_close(fd) >= 0);
        goto fail;
    }

    if (!S_ISDIR(st.st_mode)) {
        pa_assert_se(pa_close(fd) >= 0);
        errno = EEXIST;
        goto fail;
    }

    if (!update_perms) {
        pa_assert_se(pa_close(fd) >= 0);
        return 0;
    }

#ifdef HAVE_FCHOWN
    if (uid == (uid_t) -1)
        uid = getuid();
    if (gid == (gid_t) -1)
        gid = getgid();
    if (((st.st_uid != uid) || (st.st_gid != gid)) && fchown(fd, uid, gid) < 0) {
        pa_assert_se(pa_close(fd) >= 0);
        goto fail;
    }
#endif

#ifdef HAVE_FCHMOD
    if ((st.st_mode & 07777) != m && fchmod(fd, m) < 0) {
        pa_assert_se(pa_close(fd) >= 0);
        goto fail;
    };
#endif

    pa_assert_se(pa_close(fd) >= 0);
}
#else
    pa_log_warn("Secure directory creation not supported on this platform.");
#endif

    return 0;

fail:
    saved_errno = errno;
    rmdir(dir);
    errno = saved_errno;

    return -1;
}

/* Return a newly allocated sting containing the parent directory of the specified file */
char *pa_parent_dir(const char *fn) {
    char *slash, *dir = pa_xstrdup(fn);

    if ((slash = (char*) pa_path_get_filename(dir)) == dir) {
        pa_xfree(dir);
        errno = ENOENT;
        return NULL;
    }

    *(slash-1) = 0;
    return dir;
}

/* Creates a the parent directory of the specified path securely */
int pa_make_secure_parent_dir(const char *fn, mode_t m, uid_t uid, gid_t gid, bool update_perms) {
    int ret = -1;
    char *dir;

    if (!(dir = pa_parent_dir(fn)))
        goto finish;

    if (pa_make_secure_dir(dir, m, uid, gid, update_perms) < 0)
        goto finish;

    ret = 0;

finish:
    pa_xfree(dir);
    return ret;
}

/** Platform independent read function. Necessary since not all
 * systems treat all file descriptors equal. If type is
 * non-NULL it is used to cache the type of the fd. This is
 * useful for making sure that only a single syscall is executed per
 * function call. The variable pointed to should be initialized to 0
 * by the caller. */
ssize_t pa_read(int fd, void *buf, size_t count, int *type) {

#ifdef OS_IS_WIN32

    if (!type || *type == 0) {
        ssize_t r;

        if ((r = recv(fd, buf, count, 0)) >= 0)
            return r;

        if (WSAGetLastError() != WSAENOTSOCK) {
            errno = WSAGetLastError();
            return r;
        }

        if (type)
            *type = 1;
    }

#endif

    for (;;) {
        ssize_t r;

        if ((r = read(fd, buf, count)) < 0)
            if (errno == EINTR)
                continue;

        return r;
    }
}

/** Similar to pa_read(), but handles writes */
ssize_t pa_write(int fd, const void *buf, size_t count, int *type) {

    if (!type || *type == 0) {
        ssize_t r;

        for (;;) {
            if ((r = send(fd, buf, count, MSG_NOSIGNAL)) < 0) {

                if (errno == EINTR)
                    continue;

                break;
            }

            return r;
        }

#ifdef OS_IS_WIN32
        if (WSAGetLastError() != WSAENOTSOCK) {
            errno = WSAGetLastError();
            return r;
        }
#else
        if (errno != ENOTSOCK)
            return r;
#endif

        if (type)
            *type = 1;
    }

    for (;;) {
        ssize_t r;

        if ((r = write(fd, buf, count)) < 0)
            if (errno == EINTR)
                continue;

        return r;
    }
}

/** Calls read() in a loop. Makes sure that as much as 'size' bytes,
 * unless EOF is reached or an error occurred */
ssize_t pa_loop_read(int fd, void*data, size_t size, int *type) {
    ssize_t ret = 0;
    int _type;

    pa_assert(fd >= 0);
    pa_assert(data);
    pa_assert(size);

    if (!type) {
        _type = 0;
        type = &_type;
    }

    while (size > 0) {
        ssize_t r;

        if ((r = pa_read(fd, data, size, type)) < 0)
            return r;

        if (r == 0)
            break;

        ret += r;
        data = (uint8_t*) data + r;
        size -= (size_t) r;
    }

    return ret;
}

/** Similar to pa_loop_read(), but wraps write() */
ssize_t pa_loop_write(int fd, const void*data, size_t size, int *type) {
    ssize_t ret = 0;
    int _type;

    pa_assert(fd >= 0);
    pa_assert(data);
    pa_assert(size);

    if (!type) {
        _type = 0;
        type = &_type;
    }

    while (size > 0) {
        ssize_t r;

        if ((r = pa_write(fd, data, size, type)) < 0)
            return r;

        if (r == 0)
            break;

        ret += r;
        data = (const uint8_t*) data + r;
        size -= (size_t) r;
    }

    return ret;
}

/** Platform independent close function. Necessary since not all
 * systems treat all file descriptors equal. */
int pa_close(int fd) {

#ifdef OS_IS_WIN32
    int ret;

    FD_CLR(fd, &nonblocking_fds);

    if ((ret = closesocket(fd)) == 0)
        return 0;

    if (WSAGetLastError() != WSAENOTSOCK) {
        errno = WSAGetLastError();
        return ret;
    }
#endif

    for (;;) {
        int r;

        if ((r = close(fd)) < 0)
            if (errno == EINTR)
                continue;

        return r;
    }
}

/* Print a warning messages in case that the given signal is not
 * blocked or trapped */
void pa_check_signal_is_blocked(int sig) {
#ifdef HAVE_SIGACTION
    struct sigaction sa;
    sigset_t set;

    /* If POSIX threads are supported use thread-aware
     * pthread_sigmask() function, to check if the signal is
     * blocked. Otherwise fall back to sigprocmask() */

#ifdef HAVE_PTHREAD
    if (pthread_sigmask(SIG_SETMASK, NULL, &set) < 0) {
#endif
        if (sigprocmask(SIG_SETMASK, NULL, &set) < 0) {
            pa_log("sigprocmask(): %s", pa_cstrerror(errno));
            return;
        }
#ifdef HAVE_PTHREAD
    }
#endif

    if (sigismember(&set, sig))
        return;

    /* Check whether the signal is trapped */

    if (sigaction(sig, NULL, &sa) < 0) {
        pa_log("sigaction(): %s", pa_cstrerror(errno));
        return;
    }

    if (sa.sa_handler != SIG_DFL)
        return;

    pa_log_warn("%s is not trapped. This might cause malfunction!", pa_sig2str(sig));
#else /* HAVE_SIGACTION */
    pa_log_warn("%s might not be trapped. This might cause malfunction!", pa_sig2str(sig));
#endif
}

/* The following function is based on an example from the GNU libc
 * documentation. This function is similar to GNU's asprintf(). */
char *pa_sprintf_malloc(const char *format, ...) {
    size_t size = 100;
    char *c = NULL;

    pa_assert(format);

    for(;;) {
        int r;
        va_list ap;

        c = pa_xrealloc(c, size);

        va_start(ap, format);
        r = vsnprintf(c, size, format, ap);
        va_end(ap);

        c[size-1] = 0;

        if (r > -1 && (size_t) r < size)
            return c;

        if (r > -1)    /* glibc 2.1 */
            size = (size_t) r+1;
        else           /* glibc 2.0 */
            size *= 2;
    }
}

/* Same as the previous function, but use a va_list instead of an
 * ellipsis */
char *pa_vsprintf_malloc(const char *format, va_list ap) {
    size_t size = 100;
    char *c = NULL;

    pa_assert(format);

    for(;;) {
        int r;
        va_list aq;

        c = pa_xrealloc(c, size);

        va_copy(aq, ap);
        r = vsnprintf(c, size, format, aq);
        va_end(aq);

        c[size-1] = 0;

        if (r > -1 && (size_t) r < size)
            return c;

        if (r > -1)    /* glibc 2.1 */
            size = (size_t) r+1;
        else           /* glibc 2.0 */
            size *= 2;
    }
}

/* Similar to OpenBSD's strlcpy() function */
char *pa_strlcpy(char *b, const char *s, size_t l) {
    size_t k;

    pa_assert(b);
    pa_assert(s);
    pa_assert(l > 0);

    k = strlen(s);

    if (k > l-1)
        k = l-1;

    memcpy(b, s, k);
    b[k] = 0;

    return b;
}

#ifdef HAVE_SYS_RESOURCE_H
static int set_nice(int nice_level) {
#ifdef HAVE_DBUS
    DBusError error;
    DBusConnection *bus;
    int r;

    dbus_error_init(&error);
#endif

#ifdef HAVE_SYS_RESOURCE_H
    if (setpriority(PRIO_PROCESS, 0, nice_level) >= 0) {
        pa_log_debug("setpriority() worked.");
        return 0;
    }
#endif

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

    r = rtkit_make_high_priority(bus, 0, nice_level);
    dbus_connection_close(bus);
    dbus_connection_unref(bus);

    if (r >= 0) {
        pa_log_debug("RealtimeKit worked.");
        return 0;
    }

    errno = -r;
#endif

    return -1;
}
#endif

/* Raise the priority of the current process as much as possible that
 * is <= the specified nice level..*/
int pa_raise_priority(int nice_level) {

#ifdef HAVE_SYS_RESOURCE_H
    int n;

    if (set_nice(nice_level) >= 0) {
        pa_log_info("Successfully gained nice level %i.", nice_level);
        return 0;
    }

    for (n = nice_level+1; n < 0; n++)
        if (set_nice(n) >= 0) {
            pa_log_info("Successfully acquired nice level %i, which is lower than the requested %i.", n, nice_level);
            return 0;
        }

    pa_log_info("Failed to acquire high-priority scheduling: %s", pa_cstrerror(errno));
    return -1;
#endif

#ifdef OS_IS_WIN32
    if (nice_level < 0) {
        if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
            pa_log_warn("SetPriorityClass() failed: 0x%08X", GetLastError());
            errno = EPERM;
            return -1;
        }

        pa_log_info("Successfully gained high priority class.");
    }
#endif

    return 0;
}

/* Reset the priority to normal, inverting the changes made by
 * pa_raise_priority() and pa_thread_make_realtime()*/
void pa_reset_priority(void) {
#ifdef HAVE_SYS_RESOURCE_H
    struct sched_param sp;

    setpriority(PRIO_PROCESS, 0, 0);

    pa_zero(sp);
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
#endif

#ifdef OS_IS_WIN32
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
#endif
}

/* Check whenever any substring in v matches the provided regex. */
int pa_match(const char *expr, const char *v) {
#if defined(HAVE_REGEX_H) || defined(HAVE_PCREPOSIX_H)
    int k;
    regex_t re;
    int r;

    pa_assert(expr);
    pa_assert(v);

    if (regcomp(&re, expr, REG_NOSUB|REG_EXTENDED) != 0) {
        errno = EINVAL;
        return -1;
    }

    if ((k = regexec(&re, v, 0, NULL, 0)) == 0)
        r = 1;
    else if (k == REG_NOMATCH)
        r = 0;
    else
        r = -1;

    regfree(&re);

    if (r < 0)
        errno = EINVAL;

    return r;
#else
    errno = ENOSYS;
    return -1;
#endif
}

/* Check whenever the provided regex pattern is valid. */
bool pa_is_regex_valid(const char *expr) {
#if defined(HAVE_REGEX_H) || defined(HAVE_PCREPOSIX_H)
    regex_t re;

    if (expr == NULL || regcomp(&re, expr, REG_NOSUB|REG_EXTENDED) != 0) {
        return false;
    }

    regfree(&re);
    return true;
#else
    return false;
#endif
}

/* Try to parse a boolean string value.*/
int pa_parse_boolean(const char *v) {
    pa_assert(v);

    /* First we check language independent */
    if (pa_streq(v, "1") || !strcasecmp(v, "y") || !strcasecmp(v, "t")
            || !strcasecmp(v, "yes") || !strcasecmp(v, "true") || !strcasecmp(v, "on"))
        return 1;
    else if (pa_streq(v, "0") || !strcasecmp(v, "n") || !strcasecmp(v, "f")
                 || !strcasecmp(v, "no") || !strcasecmp(v, "false") || !strcasecmp(v, "off"))
        return 0;

#ifdef HAVE_LANGINFO_H
{
    const char *expr;
    /* And then we check language dependent */
    if ((expr = nl_langinfo(YESEXPR)))
        if (expr[0])
            if (pa_match(expr, v) > 0)
                return 1;

    if ((expr = nl_langinfo(NOEXPR)))
        if (expr[0])
            if (pa_match(expr, v) > 0)
                return 0;
}
#endif

    errno = EINVAL;
    return -1;
}

/* Try to parse a volume string to pa_volume_t. The allowed formats are:
 * db, % and unsigned integer */
int pa_parse_volume(const char *v, pa_volume_t *volume) {
    int len;
    uint32_t i;
    double d;
    char str[64];

    pa_assert(v);
    pa_assert(volume);

    len = strlen(v);

    if (len <= 0 || len >= 64)
        return -1;

    memcpy(str, v, len + 1);

    if (str[len - 1] == '%') {
        str[len - 1] = '\0';
        if (pa_atod(str, &d) < 0)
            return -1;

        d = d / 100 * PA_VOLUME_NORM;

        if (d < 0 || d > PA_VOLUME_MAX)
            return -1;

        *volume = d;
        return 0;
    }

    if (len > 2 && (str[len - 1] == 'b' || str[len - 1] == 'B') &&
               (str[len - 2] == 'd' || str[len - 2] == 'D')) {
        str[len - 2] = '\0';
        if (pa_atod(str, &d) < 0)
            return -1;

        if (d > pa_sw_volume_to_dB(PA_VOLUME_MAX))
            return -1;

        *volume = pa_sw_volume_from_dB(d);
        return 0;
    }

    if (pa_atou(v, &i) < 0 || !PA_VOLUME_IS_VALID(i))
        return -1;

    *volume = i;
    return 0;
}

/* Split the specified string wherever one of the characters in delimiter
 * occurs. Each time it is called returns a newly allocated string
 * with pa_xmalloc(). The variable state points to, should be
 * initialized to NULL before the first call. */
char *pa_split(const char *c, const char *delimiter, const char**state) {
    const char *current = *state ? *state : c;
    size_t l;

    if (!*current)
        return NULL;

    l = strcspn(current, delimiter);
    *state = current+l;

    if (**state)
        (*state)++;

    return pa_xstrndup(current, l);
}

/* Split the specified string wherever one of the characters in delimiter
 * occurs. Each time it is called returns a pointer to the substring within the
 * string and the length in 'n'. Note that the resultant string cannot be used
 * as-is without the length parameter, since it is merely pointing to a point
 * within the original string. The variable state points to, should be
 * initialized to NULL before the first call. */
const char *pa_split_in_place(const char *c, const char *delimiter, size_t *n, const char**state) {
    const char *current = *state ? *state : c;
    size_t l;

    if (!*current)
        return NULL;

    l = strcspn(current, delimiter);
    *state = current+l;

    if (**state)
        (*state)++;

    *n = l;
    return current;
}

/* Split a string into words. Otherwise similar to pa_split(). */
char *pa_split_spaces(const char *c, const char **state) {
    const char *current = *state ? *state : c;
    size_t l;

    if (!*current || *c == 0)
        return NULL;

    current += strspn(current, WHITESPACE);
    l = strcspn(current, WHITESPACE);

    *state = current+l;

    return pa_xstrndup(current, l);
}

/* Similar to pa_split_spaces, except this returns a string in-place.
   Returned string is generally not NULL-terminated.
   See pa_split_in_place(). */
const char *pa_split_spaces_in_place(const char *c, size_t *n, const char **state) {
    const char *current = *state ? *state : c;
    size_t l;

    if (!*current || *c == 0)
        return NULL;

    current += strspn(current, WHITESPACE);
    l = strcspn(current, WHITESPACE);

    *state = current+l;

    *n = l;
    return current;
}

PA_STATIC_TLS_DECLARE(signame, pa_xfree);

/* Return the name of an UNIX signal. Similar to Solaris sig2str() */
const char *pa_sig2str(int sig) {
    char *t;

    if (sig <= 0)
        goto fail;

#ifdef NSIG
    if (sig >= NSIG)
        goto fail;
#endif

#ifdef HAVE_SIG2STR
    {
        char buf[SIG2STR_MAX];

        if (sig2str(sig, buf) == 0) {
            pa_xfree(PA_STATIC_TLS_GET(signame));
            t = pa_sprintf_malloc("SIG%s", buf);
            PA_STATIC_TLS_SET(signame, t);
            return t;
        }
    }
#else

    switch (sig) {
#ifdef SIGHUP
        case SIGHUP:    return "SIGHUP";
#endif
        case SIGINT:    return "SIGINT";
#ifdef SIGQUIT
        case SIGQUIT:   return "SIGQUIT";
#endif
        case SIGILL:    return "SIGULL";
#ifdef SIGTRAP
        case SIGTRAP:   return "SIGTRAP";
#endif
        case SIGABRT:   return "SIGABRT";
#ifdef SIGBUS
        case SIGBUS:    return "SIGBUS";
#endif
        case SIGFPE:    return "SIGFPE";
#ifdef SIGKILL
        case SIGKILL:   return "SIGKILL";
#endif
#ifdef SIGUSR1
        case SIGUSR1:   return "SIGUSR1";
#endif
        case SIGSEGV:   return "SIGSEGV";
#ifdef SIGUSR2
        case SIGUSR2:   return "SIGUSR2";
#endif
#ifdef SIGPIPE
        case SIGPIPE:   return "SIGPIPE";
#endif
#ifdef SIGALRM
        case SIGALRM:   return "SIGALRM";
#endif
        case SIGTERM:   return "SIGTERM";
#ifdef SIGSTKFLT
        case SIGSTKFLT: return "SIGSTKFLT";
#endif
#ifdef SIGCHLD
        case SIGCHLD:   return "SIGCHLD";
#endif
#ifdef SIGCONT
        case SIGCONT:   return "SIGCONT";
#endif
#ifdef SIGSTOP
        case SIGSTOP:   return "SIGSTOP";
#endif
#ifdef SIGTSTP
        case SIGTSTP:   return "SIGTSTP";
#endif
#ifdef SIGTTIN
        case SIGTTIN:   return "SIGTTIN";
#endif
#ifdef SIGTTOU
        case SIGTTOU:   return "SIGTTOU";
#endif
#ifdef SIGURG
        case SIGURG:    return "SIGURG";
#endif
#ifdef SIGXCPU
        case SIGXCPU:   return "SIGXCPU";
#endif
#ifdef SIGXFSZ
        case SIGXFSZ:   return "SIGXFSZ";
#endif
#ifdef SIGVTALRM
        case SIGVTALRM: return "SIGVTALRM";
#endif
#ifdef SIGPROF
        case SIGPROF:   return "SIGPROF";
#endif
#ifdef SIGWINCH
        case SIGWINCH:  return "SIGWINCH";
#endif
#ifdef SIGIO
        case SIGIO:     return "SIGIO";
#endif
#ifdef SIGPWR
        case SIGPWR:    return "SIGPWR";
#endif
#ifdef SIGSYS
        case SIGSYS:    return "SIGSYS";
#endif
    }

#ifdef SIGRTMIN
    if (sig >= SIGRTMIN && sig <= SIGRTMAX) {
        pa_xfree(PA_STATIC_TLS_GET(signame));
        t = pa_sprintf_malloc("SIGRTMIN+%i", sig - SIGRTMIN);
        PA_STATIC_TLS_SET(signame, t);
        return t;
    }
#endif

#endif

fail:

    pa_xfree(PA_STATIC_TLS_GET(signame));
    t = pa_sprintf_malloc("SIG%i", sig);
    PA_STATIC_TLS_SET(signame, t);
    return t;
}

#ifdef HAVE_GRP_H

/* Check whether the specified GID and the group name match */
static int is_group(gid_t gid, const char *name) {
    struct group *group = NULL;
    int r = -1;

    errno = 0;
    if (!(group = pa_getgrgid_malloc(gid))) {
        if (!errno)
            errno = ENOENT;

        pa_log("pa_getgrgid_malloc(%u): %s", gid, pa_cstrerror(errno));

        goto finish;
    }

    r = pa_streq(name, group->gr_name);

finish:
    pa_getgrgid_free(group);

    return r;
}

/* Check the current user is member of the specified group */
int pa_own_uid_in_group(const char *name, gid_t *gid) {
    GETGROUPS_T *gids, tgid;
    long n = sysconf(_SC_NGROUPS_MAX);
    int r = -1, i, k;

    pa_assert(n > 0);

    gids = pa_xmalloc(sizeof(GETGROUPS_T) * (size_t) n);

    if ((n = getgroups((int) n, gids)) < 0) {
        pa_log("getgroups(): %s", pa_cstrerror(errno));
        goto finish;
    }

    for (i = 0; i < n; i++) {

        if ((k = is_group(gids[i], name)) < 0)
            goto finish;
        else if (k > 0) {
            *gid = gids[i];
            r = 1;
            goto finish;
        }
    }

    if ((k = is_group(tgid = getgid(), name)) < 0)
        goto finish;
    else if (k > 0) {
        *gid = tgid;
        r = 1;
        goto finish;
    }

    r = 0;

finish:

    pa_xfree(gids);
    return r;
}

/* Check whether the specific user id is a member of the specified group */
int pa_uid_in_group(uid_t uid, const char *name) {
    struct group *group = NULL;
    char **i;
    int r = -1;

    errno = 0;
    if (!(group = pa_getgrnam_malloc(name))) {
        if (!errno)
            errno = ENOENT;
        goto finish;
    }

    r = 0;
    for (i = group->gr_mem; *i; i++) {
        struct passwd *pw = NULL;

        errno = 0;
        if (!(pw = pa_getpwnam_malloc(*i)))
            continue;

        if (pw->pw_uid == uid)
            r = 1;

        pa_getpwnam_free(pw);

        if (r == 1)
            break;
    }

finish:
    pa_getgrnam_free(group);

    return r;
}

/* Get the GID of a given group, return (gid_t) -1 on failure. */
gid_t pa_get_gid_of_group(const char *name) {
    gid_t ret = (gid_t) -1;
    struct group *gr = NULL;

    errno = 0;
    if (!(gr = pa_getgrnam_malloc(name))) {
        if (!errno)
            errno = ENOENT;
        goto finish;
    }

    ret = gr->gr_gid;

finish:
    pa_getgrnam_free(gr);
    return ret;
}

int pa_check_in_group(gid_t g) {
    gid_t gids[NGROUPS_MAX];
    int r;

    if ((r = getgroups(NGROUPS_MAX, gids)) < 0)
        return -1;

    for (; r > 0; r--)
        if (gids[r-1] == g)
            return 1;

    return 0;
}

#else /* HAVE_GRP_H */

int pa_own_uid_in_group(const char *name, gid_t *gid) {
    errno = ENOTSUP;
    return -1;

}

int pa_uid_in_group(uid_t uid, const char *name) {
    errno = ENOTSUP;
    return -1;
}

gid_t pa_get_gid_of_group(const char *name) {
    errno = ENOTSUP;
    return (gid_t) -1;
}

int pa_check_in_group(gid_t g) {
    errno = ENOTSUP;
    return -1;
}

#endif

/* Lock or unlock a file entirely.
  (advisory on UNIX, mandatory on Windows) */
int pa_lock_fd(int fd, int b) {
#ifdef F_SETLKW
    struct flock f_lock;

    /* Try a R/W lock first */

    f_lock.l_type = (short) (b ? F_WRLCK : F_UNLCK);
    f_lock.l_whence = SEEK_SET;
    f_lock.l_start = 0;
    f_lock.l_len = 0;

    if (fcntl(fd, F_SETLKW, &f_lock) >= 0)
        return 0;

    /* Perhaps the file descriptor was opened for read only, than try again with a read lock. */
    if (b && errno == EBADF) {
        f_lock.l_type = F_RDLCK;
        if (fcntl(fd, F_SETLKW, &f_lock) >= 0)
            return 0;
    }

    pa_log("%slock: %s", !b ? "un" : "", pa_cstrerror(errno));
#endif

#ifdef OS_IS_WIN32
    HANDLE h = (HANDLE) _get_osfhandle(fd);

    if (b && LockFile(h, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF))
        return 0;
    if (!b && UnlockFile(h, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF))
        return 0;

    pa_log("%slock failed: 0x%08X", !b ? "un" : "", GetLastError());

    /* FIXME: Needs to set errno! */
#endif

    return -1;
}

/* Remove trailing newlines from a string */
char* pa_strip_nl(char *s) {
    pa_assert(s);

    s[strcspn(s, NEWLINE)] = 0;
    return s;
}

char *pa_strip(char *s) {
    char *e, *l = NULL;

    /* Drops trailing whitespace. Modifies the string in
     * place. Returns pointer to first non-space character */

    s += strspn(s, WHITESPACE);

    for (e = s; *e; e++)
        if (!strchr(WHITESPACE, *e))
            l = e;

    if (l)
        *(l+1) = 0;
    else
        *s = 0;

    return s;
}

/* Create a temporary lock file and lock it. */
int pa_lock_lockfile(const char *fn) {
    int fd;
    pa_assert(fn);

    for (;;) {
        struct stat st;

        if ((fd = pa_open_cloexec(fn, O_CREAT|O_RDWR
#ifdef O_NOFOLLOW
                       |O_NOFOLLOW
#endif
                       , S_IRUSR|S_IWUSR)) < 0) {
            pa_log_warn("Failed to create lock file '%s': %s", fn, pa_cstrerror(errno));
            goto fail;
        }

        if (pa_lock_fd(fd, 1) < 0) {
            pa_log_warn("Failed to lock file '%s'.", fn);
            goto fail;
        }

        if (fstat(fd, &st) < 0) {
            pa_log_warn("Failed to fstat() file '%s': %s", fn, pa_cstrerror(errno));
            goto fail;
        }

        /* Check whether the file has been removed meanwhile. When yes,
         * restart this loop, otherwise, we're done */
        if (st.st_nlink >= 1)
            break;

        if (pa_lock_fd(fd, 0) < 0) {
            pa_log_warn("Failed to unlock file '%s'.", fn);
            goto fail;
        }

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
        pa_close(fd);
        errno = saved_errno;
    }

    return -1;
}

/* Unlock a temporary lock file */
int pa_unlock_lockfile(const char *fn, int fd) {
    int r = 0;
    pa_assert(fd >= 0);

    if (fn) {
        if (unlink(fn) < 0) {
            pa_log_warn("Unable to remove lock file '%s': %s", fn, pa_cstrerror(errno));
            r = -1;
        }
    }

    if (pa_lock_fd(fd, 0) < 0) {
        pa_log_warn("Failed to unlock file '%s'.", fn);
        r = -1;
    }

    if (pa_close(fd) < 0) {
        pa_log_warn("Failed to close '%s': %s", fn, pa_cstrerror(errno));
        r = -1;
    }

    return r;
}

static int check_ours(const char *p) {
    struct stat st;

    pa_assert(p);

    if (stat(p, &st) < 0)
        return -errno;

#ifdef HAVE_GETUID
    if (st.st_uid != getuid() && st.st_uid != 0)
        return -EACCES;
#endif

    return 0;
}

static char *get_pulse_home(void) {
    char *h, *ret;
    int t;

    h = pa_get_home_dir_malloc();
    if (!h) {
        pa_log_error("Failed to get home directory.");
        return NULL;
    }

    t = check_ours(h);
    if (t < 0 && t != -ENOENT) {
        pa_log_error("Home directory not accessible: %s", pa_cstrerror(-t));
        pa_xfree(h);
        return NULL;
    }

    /* If the old directory exists, use it. */
    ret = pa_sprintf_malloc("%s" PA_PATH_SEP ".pulse", h);
    pa_xfree(h);
    if (access(ret, F_OK) >= 0)
        return ret;
    free(ret);

    /* Otherwise go for the XDG compliant directory. */
    if (pa_get_config_home_dir(&ret) < 0)
        return NULL;

    return ret;
}

char *pa_get_state_dir(void) {
    char *d;

    /* The state directory shall contain dynamic data that should be
     * kept across reboots, and is private to this user */

    if (!(d = pa_xstrdup(getenv("PULSE_STATE_PATH"))))
        if (!(d = get_pulse_home()))
            return NULL;

    /* If PULSE_STATE_PATH and PULSE_RUNTIME_PATH point to the same
     * dir then this will break. */

    if (pa_make_secure_dir(d, 0700U, (uid_t) -1, (gid_t) -1, true) < 0) {
        pa_log_error("Failed to create secure directory (%s): %s", d, pa_cstrerror(errno));
        pa_xfree(d);
        return NULL;
    }

    return d;
}

char *pa_get_home_dir_malloc(void) {
    char *homedir;
    size_t allocated = 128;

    for (;;) {
        homedir = pa_xmalloc(allocated);

        if (!pa_get_home_dir(homedir, allocated)) {
            pa_xfree(homedir);
            return NULL;
        }

        if (strlen(homedir) < allocated - 1)
            break;

        pa_xfree(homedir);
        allocated *= 2;
    }

    return homedir;
}

int pa_append_to_home_dir(const char *path, char **_r) {
    char *home_dir;

    pa_assert(path);
    pa_assert(_r);

    home_dir = pa_get_home_dir_malloc();
    if (!home_dir) {
        pa_log("Failed to get home directory.");
        return -PA_ERR_NOENTITY;
    }

    *_r = pa_sprintf_malloc("%s" PA_PATH_SEP "%s", home_dir, path);
    pa_xfree(home_dir);
    return 0;
}

int pa_get_config_home_dir(char **_r) {
    const char *e;
    char *home_dir;

    pa_assert(_r);

    e = getenv("XDG_CONFIG_HOME");
    if (e && *e) {
        *_r = pa_sprintf_malloc("%s" PA_PATH_SEP "pulse", e);
        return 0;
    }

    home_dir = pa_get_home_dir_malloc();
    if (!home_dir)
        return -PA_ERR_NOENTITY;

    *_r = pa_sprintf_malloc("%s" PA_PATH_SEP ".config" PA_PATH_SEP "pulse", home_dir);
    pa_xfree(home_dir);
    return 0;
}

int pa_append_to_config_home_dir(const char *path, char **_r) {
    int r;
    char *config_home_dir;

    pa_assert(path);
    pa_assert(_r);

    r = pa_get_config_home_dir(&config_home_dir);
    if (r < 0)
        return r;

    *_r = pa_sprintf_malloc("%s" PA_PATH_SEP "%s", config_home_dir, path);
    pa_xfree(config_home_dir);
    return 0;
}

char *pa_get_binary_name_malloc(void) {
    char *t;
    size_t allocated = 128;

    for (;;) {
        t = pa_xmalloc(allocated);

        if (!pa_get_binary_name(t, allocated)) {
            pa_xfree(t);
            return NULL;
        }

        if (strlen(t) < allocated - 1)
            break;

        pa_xfree(t);
        allocated *= 2;
    }

    return t;
}

static char* make_random_dir(mode_t m) {
    static const char table[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    char *fn;
    size_t pathlen;

    fn = pa_sprintf_malloc("%s" PA_PATH_SEP "pulse-XXXXXXXXXXXX", pa_get_temp_dir());
    pathlen = strlen(fn);

    for (;;) {
        size_t i;
        int r;
        mode_t u;
        int saved_errno;

        for (i = pathlen - 12; i < pathlen; i++)
            fn[i] = table[rand() % (sizeof(table)-1)];

        u = umask((~m) & 0777);
#ifndef OS_IS_WIN32
        r = mkdir(fn, m);
#else
        r = mkdir(fn);
#endif

        saved_errno = errno;
        umask(u);
        errno = saved_errno;

        if (r >= 0)
            return fn;

        if (errno != EEXIST) {
            pa_log_error("Failed to create random directory %s: %s", fn, pa_cstrerror(errno));
            pa_xfree(fn);
            return NULL;
        }
    }
}

static int make_random_dir_and_link(mode_t m, const char *k) {
    char *p;

    if (!(p = make_random_dir(m)))
        return -1;

#ifdef HAVE_SYMLINK
    if (symlink(p, k) < 0) {
        int saved_errno = errno;

        if (errno != EEXIST)
            pa_log_error("Failed to symlink %s to %s: %s", k, p, pa_cstrerror(errno));

        rmdir(p);
        pa_xfree(p);

        errno = saved_errno;
        return -1;
    }
#else
    pa_xfree(p);
    return -1;
#endif

    pa_xfree(p);
    return 0;
}

char *pa_get_runtime_dir(void) {
    char *d, *k = NULL, *p = NULL, *t = NULL, *mid;
    mode_t m;

    /* The runtime directory shall contain dynamic data that needs NOT
     * to be kept across reboots and is usually private to the user,
     * except in system mode, where it might be accessible by other
     * users, too. Since we need POSIX locking and UNIX sockets in
     * this directory, we try XDG_RUNTIME_DIR first, and if that isn't
     * set create a directory in $HOME and link it to a random subdir
     * in /tmp, if it was not explicitly configured. */

    m = pa_in_system_mode() ? 0755U : 0700U;

    /* Use the explicitly configured value if it is set */
    d = getenv("PULSE_RUNTIME_PATH");
    if (d) {

        if (pa_make_secure_dir(d, m, (uid_t) -1, (gid_t) -1, true) < 0) {
            pa_log_error("Failed to create secure directory (%s): %s", d, pa_cstrerror(errno));
            goto fail;
        }

        return pa_xstrdup(d);
    }

    /* Use the XDG standard for the runtime directory. */
    d = getenv("XDG_RUNTIME_DIR");
    if (d) {
#ifdef HAVE_GETUID
        struct stat st;
        if (stat(d, &st) == 0 && st.st_uid != getuid()) {
            pa_log(_("XDG_RUNTIME_DIR (%s) is not owned by us (uid %d), but by uid %d! "
                   "(This could e.g. happen if you try to connect to a non-root PulseAudio as a root user, over the native protocol. Don't do that.)"),
                   d, getuid(), st.st_uid);
            goto fail;
        }
#endif

        k = pa_sprintf_malloc("%s" PA_PATH_SEP "pulse", d);

        if (pa_make_secure_dir(k, m, (uid_t) -1, (gid_t) -1, true) < 0) {
            pa_log_error("Failed to create secure directory (%s): %s", k, pa_cstrerror(errno));
            goto fail;
        }

        return k;
    }

    /* XDG_RUNTIME_DIR wasn't set, use the old legacy fallback */
    d = get_pulse_home();
    if (!d)
        goto fail;

    if (pa_make_secure_dir(d, m, (uid_t) -1, (gid_t) -1, true) < 0) {
        pa_log_error("Failed to create secure directory (%s): %s", d, pa_cstrerror(errno));
        pa_xfree(d);
        goto fail;
    }

    mid = pa_machine_id();
    if (!mid) {
        pa_xfree(d);
        goto fail;
    }

    k = pa_sprintf_malloc("%s" PA_PATH_SEP "%s-runtime", d, mid);
    pa_xfree(d);
    pa_xfree(mid);

    for (;;) {
        /* OK, first let's check if the "runtime" symlink already exists */

        p = pa_readlink(k);
        if (!p) {

            if (errno != ENOENT) {
                pa_log_error("Failed to stat runtime directory %s: %s", k, pa_cstrerror(errno));
                goto fail;
            }

#ifdef HAVE_SYMLINK
            /* Hmm, so the runtime directory didn't exist yet, so let's
             * create one in /tmp and symlink that to it */

            if (make_random_dir_and_link(0700, k) < 0) {

                /* Mhmm, maybe another process was quicker than us,
                 * let's check if that was valid */
                if (errno == EEXIST)
                    continue;

                goto fail;
            }
#else
            /* No symlink possible, so let's just create the runtime directly
             * Do not check again if it exists since it cannot be a symlink */
            if (mkdir(k) < 0 && errno != EEXIST)
                goto fail;
#endif

            return k;
        }

        /* Make sure that this actually makes sense */
        if (!pa_is_path_absolute(p)) {
            pa_log_error("Path %s in link %s is not absolute.", p, k);
            errno = ENOENT;
            goto fail;
        }

        /* Hmm, so this symlink is still around, make sure nobody fools us */
#ifdef HAVE_LSTAT
{
        struct stat st;
        if (lstat(p, &st) < 0) {

            if (errno != ENOENT) {
                pa_log_error("Failed to stat runtime directory %s: %s", p, pa_cstrerror(errno));
                goto fail;
            }

        } else {

            if (S_ISDIR(st.st_mode) &&
                (st.st_uid == getuid()) &&
                ((st.st_mode & 0777) == 0700)) {

                pa_xfree(p);
                return k;
            }

            pa_log_info("Hmm, runtime path exists, but points to an invalid directory. Changing runtime directory.");
        }
}
#endif

        pa_xfree(p);
        p = NULL;

        /* Hmm, so the link points to some nonexisting or invalid
         * dir. Let's replace it by a new link. We first create a
         * temporary link and then rename that to allow concurrent
         * execution of this function. */

        t = pa_sprintf_malloc("%s.tmp", k);

        if (make_random_dir_and_link(0700, t) < 0) {

            if (errno != EEXIST) {
                pa_log_error("Failed to symlink %s: %s", t, pa_cstrerror(errno));
                goto fail;
            }

            pa_xfree(t);
            t = NULL;

            /* Hmm, someone else was quicker then us. Let's give
             * him some time to finish, and retry. */
            pa_msleep(10);
            continue;
        }

        /* OK, we succeeded in creating the temporary symlink, so
         * let's rename it */
        if (rename(t, k) < 0) {
            pa_log_error("Failed to rename %s to %s: %s", t, k, pa_cstrerror(errno));
            goto fail;
        }

        pa_xfree(t);
        return k;
    }

fail:
    pa_xfree(p);
    pa_xfree(k);
    pa_xfree(t);

    return NULL;
}

/* Try to open a configuration file. If "env" is specified, open the
 * value of the specified environment variable. Otherwise look for a
 * file "local" in the home directory or a file "global" in global
 * file system. If "result" is non-NULL, a pointer to a newly
 * allocated buffer containing the used configuration file is
 * stored there.*/
FILE *pa_open_config_file(const char *global, const char *local, const char *env, char **result) {
    const char *fn;
    FILE *f;

    if (env && (fn = getenv(env))) {
        if ((f = pa_fopen_cloexec(fn, "r"))) {
            if (result)
                *result = pa_xstrdup(fn);

            return f;
        }

        pa_log_warn("Failed to open configuration file '%s': %s", fn, pa_cstrerror(errno));
        return NULL;
    }

    if (local) {
        const char *e;
        char *lfn;
        char *h;

        if ((e = getenv("PULSE_CONFIG_PATH"))) {
            fn = lfn = pa_sprintf_malloc("%s" PA_PATH_SEP "%s", e, local);
            f = pa_fopen_cloexec(fn, "r");
        } else if ((h = pa_get_home_dir_malloc())) {
            fn = lfn = pa_sprintf_malloc("%s" PA_PATH_SEP ".pulse" PA_PATH_SEP "%s", h, local);
            f = pa_fopen_cloexec(fn, "r");
            if (!f) {
                free(lfn);
                fn = lfn = pa_sprintf_malloc("%s" PA_PATH_SEP ".config/pulse" PA_PATH_SEP "%s", h, local);
                f = pa_fopen_cloexec(fn, "r");
            }
            pa_xfree(h);
        } else
            return NULL;

        if (f) {
            if (result)
                *result = pa_xstrdup(fn);

            pa_xfree(lfn);
            return f;
        }

        if (errno != ENOENT) {
            pa_log_warn("Failed to open configuration file '%s': %s", fn, pa_cstrerror(errno));
            pa_xfree(lfn);
            return NULL;
        }

        pa_xfree(lfn);
    }

    if (global) {
        char *gfn;

#ifdef OS_IS_WIN32
        if (strncmp(global, PA_DEFAULT_CONFIG_DIR, strlen(PA_DEFAULT_CONFIG_DIR)) == 0)
            gfn = pa_sprintf_malloc("%s" PA_PATH_SEP "etc" PA_PATH_SEP "pulse%s",
                                    pa_win32_get_toplevel(NULL),
                                    global + strlen(PA_DEFAULT_CONFIG_DIR));
        else
#endif
        gfn = pa_xstrdup(global);

        if ((f = pa_fopen_cloexec(gfn, "r"))) {
            if (result)
                *result = gfn;
            else
                pa_xfree(gfn);

            return f;
        }
        pa_xfree(gfn);
    }

    errno = ENOENT;
    return NULL;
}

char *pa_find_config_file(const char *global, const char *local, const char *env) {
    const char *fn;

    if (env && (fn = getenv(env))) {
        if (access(fn, R_OK) == 0)
            return pa_xstrdup(fn);

        pa_log_warn("Failed to access configuration file '%s': %s", fn, pa_cstrerror(errno));
        return NULL;
    }

    if (local) {
        const char *e;
        char *lfn;
        char *h;

        if ((e = getenv("PULSE_CONFIG_PATH")))
            fn = lfn = pa_sprintf_malloc("%s" PA_PATH_SEP "%s", e, local);
        else if ((h = pa_get_home_dir_malloc())) {
            fn = lfn = pa_sprintf_malloc("%s" PA_PATH_SEP ".pulse" PA_PATH_SEP "%s", h, local);
            pa_xfree(h);
        } else
            return NULL;

        if (access(fn, R_OK) == 0) {
            char *r = pa_xstrdup(fn);
            pa_xfree(lfn);
            return r;
        }

        if (errno != ENOENT) {
            pa_log_warn("Failed to access configuration file '%s': %s", fn, pa_cstrerror(errno));
            pa_xfree(lfn);
            return NULL;
        }

        pa_xfree(lfn);
    }

    if (global) {
        char *gfn;

#ifdef OS_IS_WIN32
        if (strncmp(global, PA_DEFAULT_CONFIG_DIR, strlen(PA_DEFAULT_CONFIG_DIR)) == 0)
            gfn = pa_sprintf_malloc("%s" PA_PATH_SEP "etc" PA_PATH_SEP "pulse%s",
                                    pa_win32_get_toplevel(NULL),
                                    global + strlen(PA_DEFAULT_CONFIG_DIR));
        else
#endif
        gfn = pa_xstrdup(global);

        if (access(gfn, R_OK) == 0)
            return gfn;
        pa_xfree(gfn);
    }

    errno = ENOENT;

    return NULL;
}

/* Format the specified data as a hexademical string */
char *pa_hexstr(const uint8_t* d, size_t dlength, char *s, size_t slength) {
    size_t i = 0, j = 0;
    const char hex[] = "0123456789abcdef";

    pa_assert(d);
    pa_assert(s);
    pa_assert(slength > 0);

    while (j+2 < slength && i < dlength) {
        s[j++] = hex[*d >> 4];
        s[j++] = hex[*d & 0xF];

        d++;
        i++;
    }

    s[j < slength ? j : slength] = 0;
    return s;
}

/* Convert a hexadecimal digit to a number or -1 if invalid */
static int hexc(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    errno = EINVAL;
    return -1;
}

/* Parse a hexadecimal string as created by pa_hexstr() to a BLOB */
size_t pa_parsehex(const char *p, uint8_t *d, size_t dlength) {
    size_t j = 0;

    pa_assert(p);
    pa_assert(d);

    while (j < dlength && *p) {
        int b;

        if ((b = hexc(*(p++))) < 0)
            return (size_t) -1;

        d[j] = (uint8_t) (b << 4);

        if (!*p)
            return (size_t) -1;

        if ((b = hexc(*(p++))) < 0)
            return (size_t) -1;

        d[j] |= (uint8_t) b;
        j++;
    }

    return j;
}

/* Returns nonzero when *s starts with *pfx */
bool pa_startswith(const char *s, const char *pfx) {
    size_t l;

    pa_assert(s);
    pa_assert(pfx);

    l = strlen(pfx);

    return strlen(s) >= l && strncmp(s, pfx, l) == 0;
}

/* Returns nonzero when *s ends with *sfx */
bool pa_endswith(const char *s, const char *sfx) {
    size_t l1, l2;

    pa_assert(s);
    pa_assert(sfx);

    l1 = strlen(s);
    l2 = strlen(sfx);

    return l1 >= l2 && pa_streq(s + l1 - l2, sfx);
}

bool pa_is_path_absolute(const char *fn) {
    pa_assert(fn);

#ifndef OS_IS_WIN32
    return *fn == '/';
#else
    return strlen(fn) >= 3 && isalpha(fn[0]) && fn[1] == ':' && fn[2] == '\\';
#endif
}

char *pa_make_path_absolute(const char *p) {
    char *r;
    char *cwd;

    pa_assert(p);

    if (pa_is_path_absolute(p))
        return pa_xstrdup(p);

    if (!(cwd = pa_getcwd()))
        return pa_xstrdup(p);

    r = pa_sprintf_malloc("%s" PA_PATH_SEP "%s", cwd, p);
    pa_xfree(cwd);
    return r;
}

/* If fn is NULL, return the PulseAudio runtime or state dir (depending on the
 * rt parameter). If fn is non-NULL and starts with /, return fn. Otherwise,
 * append fn to the runtime/state dir and return it. */
static char *get_path(const char *fn, bool prependmid, bool rt) {
    char *rtp;

    rtp = rt ? pa_get_runtime_dir() : pa_get_state_dir();

    if (fn) {
        char *r, *canonical_rtp;

        if (pa_is_path_absolute(fn)) {
            pa_xfree(rtp);
            return pa_xstrdup(fn);
        }

        if (!rtp)
            return NULL;

        /* Hopefully make the path smaller to avoid 108 char limit (fdo#44680) */
        if ((canonical_rtp = pa_realpath(rtp))) {
            if (strlen(rtp) >= strlen(canonical_rtp))
                pa_xfree(rtp);
            else {
                pa_xfree(canonical_rtp);
                canonical_rtp = rtp;
            }
        } else
            canonical_rtp = rtp;

        if (prependmid) {
            char *mid;

            if (!(mid = pa_machine_id())) {
                pa_xfree(canonical_rtp);
                return NULL;
            }

            r = pa_sprintf_malloc("%s" PA_PATH_SEP "%s-%s", canonical_rtp, mid, fn);
            pa_xfree(mid);
        } else
            r = pa_sprintf_malloc("%s" PA_PATH_SEP "%s", canonical_rtp, fn);

        pa_xfree(canonical_rtp);
        return r;
    } else
        return rtp;
}

char *pa_runtime_path(const char *fn) {
    return get_path(fn, false, true);
}

char *pa_state_path(const char *fn, bool appendmid) {
    return get_path(fn, appendmid, false);
}

/* Convert the string s to a signed integer in *ret_i */
int pa_atoi(const char *s, int32_t *ret_i) {
    long l;

    pa_assert(s);
    pa_assert(ret_i);

    if (pa_atol(s, &l) < 0)
        return -1;

    if ((int32_t) l != l) {
        errno = ERANGE;
        return -1;
    }

    *ret_i = (int32_t) l;

    return 0;
}

/* Convert the string s to an unsigned integer in *ret_u */
int pa_atou(const char *s, uint32_t *ret_u) {
    char *x = NULL;
    unsigned long l;

    pa_assert(s);
    pa_assert(ret_u);

    /* strtoul() ignores leading spaces. We don't. */
    if (isspace((unsigned char)*s)) {
        errno = EINVAL;
        return -1;
    }

    /* strtoul() accepts strings that start with a minus sign. In that case the
     * original negative number gets negated, and strtoul() returns the negated
     * result. We don't want that kind of behaviour. strtoul() also allows a
     * leading plus sign, which is also a thing that we don't want. */
    if (*s == '-' || *s == '+') {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    l = strtoul(s, &x, 0);

    /* If x doesn't point to the end of s, there was some trailing garbage in
     * the string. If x points to s, no conversion was done (empty string). */
    if (!x || *x || x == s || errno) {
        if (!errno)
            errno = EINVAL;
        return -1;
    }

    if ((uint32_t) l != l) {
        errno = ERANGE;
        return -1;
    }

    *ret_u = (uint32_t) l;

    return 0;
}

/* Convert the string s to a signed long integer in *ret_l. */
int pa_atol(const char *s, long *ret_l) {
    char *x = NULL;
    long l;

    pa_assert(s);
    pa_assert(ret_l);

    /* strtol() ignores leading spaces. We don't. */
    if (isspace((unsigned char)*s)) {
        errno = EINVAL;
        return -1;
    }

    /* strtol() accepts leading plus signs, but that's ugly, so we don't allow
     * that. */
    if (*s == '+') {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    l = strtol(s, &x, 0);

    /* If x doesn't point to the end of s, there was some trailing garbage in
     * the string. If x points to s, no conversion was done (at least an empty
     * string can trigger this). */
    if (!x || *x || x == s || errno) {
        if (!errno)
            errno = EINVAL;
        return -1;
    }

    *ret_l = l;

    return 0;
}

#ifdef HAVE_STRTOD_L
static locale_t c_locale = NULL;

static void c_locale_destroy(void) {
    freelocale(c_locale);
}
#endif

int pa_atod(const char *s, double *ret_d) {
    char *x = NULL;
    double f;

    pa_assert(s);
    pa_assert(ret_d);

    /* strtod() ignores leading spaces. We don't. */
    if (isspace((unsigned char)*s)) {
        errno = EINVAL;
        return -1;
    }

    /* strtod() accepts leading plus signs, but that's ugly, so we don't allow
     * that. */
    if (*s == '+') {
        errno = EINVAL;
        return -1;
    }

    /* This should be locale independent */

#ifdef HAVE_STRTOD_L

    PA_ONCE_BEGIN {

        if ((c_locale = newlocale(LC_ALL_MASK, "C", NULL)))
            atexit(c_locale_destroy);

    } PA_ONCE_END;

    if (c_locale) {
        errno = 0;
        f = strtod_l(s, &x, c_locale);
    } else
#endif
    {
        errno = 0;
        f = strtod(s, &x);
    }

    /* If x doesn't point to the end of s, there was some trailing garbage in
     * the string. If x points to s, no conversion was done (at least an empty
     * string can trigger this). */
    if (!x || *x || x == s || errno) {
        if (!errno)
            errno = EINVAL;
        return -1;
    }

    if (isnan(f)) {
        errno = EINVAL;
        return -1;
    }

    *ret_d = f;

    return 0;
}

/* Same as snprintf, but guarantees NUL-termination on every platform */
size_t pa_snprintf(char *str, size_t size, const char *format, ...) {
    size_t ret;
    va_list ap;

    pa_assert(str);
    pa_assert(size > 0);
    pa_assert(format);

    va_start(ap, format);
    ret = pa_vsnprintf(str, size, format, ap);
    va_end(ap);

    return ret;
}

/* Same as vsnprintf, but guarantees NUL-termination on every platform */
size_t pa_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    int ret;

    pa_assert(str);
    pa_assert(size > 0);
    pa_assert(format);

    ret = vsnprintf(str, size, format, ap);

    str[size-1] = 0;

    if (ret < 0)
        return strlen(str);

    if ((size_t) ret > size-1)
        return size-1;

    return (size_t) ret;
}

/* Truncate the specified string, but guarantee that the string
 * returned still validates as UTF8 */
char *pa_truncate_utf8(char *c, size_t l) {
    pa_assert(c);
    pa_assert(pa_utf8_valid(c));

    if (strlen(c) <= l)
        return c;

    c[l] = 0;

    while (l > 0 && !pa_utf8_valid(c))
        c[--l] = 0;

    return c;
}

char *pa_getcwd(void) {
    size_t l = 128;

    for (;;) {
        char *p = pa_xmalloc(l);
        if (getcwd(p, l))
            return p;

        if (errno != ERANGE) {
            pa_xfree(p);
            return NULL;
        }

        pa_xfree(p);
        l *= 2;
    }
}

void *pa_will_need(const void *p, size_t l) {
#ifdef RLIMIT_MEMLOCK
    struct rlimit rlim;
#endif
    const void *a;
    size_t size;
    int r = ENOTSUP;
    size_t bs;
    const size_t page_size = pa_page_size();

    pa_assert(p);
    pa_assert(l > 0);

    a = PA_PAGE_ALIGN_PTR(p);
    size = (size_t) ((const uint8_t*) p + l - (const uint8_t*) a);

#ifdef HAVE_POSIX_MADVISE
    if ((r = posix_madvise((void*) a, size, POSIX_MADV_WILLNEED)) == 0) {
        pa_log_debug("posix_madvise() worked fine!");
        return (void*) p;
    }
#endif

    /* Most likely the memory was not mmap()ed from a file and thus
     * madvise() didn't work, so let's misuse mlock() do page this
     * stuff back into RAM. Yeah, let's fuck with the MM!  It's so
     * inviting, the man page of mlock() tells us: "All pages that
     * contain a part of the specified address range are guaranteed to
     * be resident in RAM when the call returns successfully." */

#ifdef RLIMIT_MEMLOCK
    pa_assert_se(getrlimit(RLIMIT_MEMLOCK, &rlim) == 0);

    if (rlim.rlim_cur < page_size) {
        pa_log_debug("posix_madvise() failed (or doesn't exist), resource limits don't allow mlock(), can't page in data: %s", pa_cstrerror(r));
        errno = EPERM;
        return (void*) p;
    }

    bs = PA_PAGE_ALIGN((size_t) rlim.rlim_cur);
#else
    bs = page_size*4;
#endif

    pa_log_debug("posix_madvise() failed (or doesn't exist), trying mlock(): %s", pa_cstrerror(r));

#ifdef HAVE_MLOCK
    while (size > 0 && bs > 0) {

        if (bs > size)
            bs = size;

        if (mlock(a, bs) < 0) {
            bs = PA_PAGE_ALIGN(bs / 2);
            continue;
        }

        pa_assert_se(munlock(a, bs) == 0);

        a = (const uint8_t*) a + bs;
        size -= bs;
    }
#endif

    if (bs <= 0)
        pa_log_debug("mlock() failed too (or doesn't exist), giving up: %s", pa_cstrerror(errno));
    else
        pa_log_debug("mlock() worked fine!");

    return (void*) p;
}

void pa_close_pipe(int fds[2]) {
    pa_assert(fds);

    if (fds[0] >= 0)
        pa_assert_se(pa_close(fds[0]) == 0);

    if (fds[1] >= 0)
        pa_assert_se(pa_close(fds[1]) == 0);

    fds[0] = fds[1] = -1;
}

char *pa_readlink(const char *p) {
#ifdef HAVE_READLINK
    size_t l = 100;

    for (;;) {
        char *c;
        ssize_t n;

        c = pa_xmalloc(l);

        if ((n = readlink(p, c, l-1)) < 0) {
            pa_xfree(c);
            return NULL;
        }

        if ((size_t) n < l-1) {
            c[n] = 0;
            return c;
        }

        pa_xfree(c);
        l *= 2;
    }
#else
    return NULL;
#endif
}

int pa_close_all(int except_fd, ...) {
    va_list ap;
    unsigned n = 0, i;
    int r, *p;

    va_start(ap, except_fd);

    if (except_fd >= 0)
        for (n = 1; va_arg(ap, int) >= 0; n++)
            ;

    va_end(ap);

    p = pa_xnew(int, n+1);

    va_start(ap, except_fd);

    i = 0;
    if (except_fd >= 0) {
        int fd;
        p[i++] = except_fd;

        while ((fd = va_arg(ap, int)) >= 0)
            p[i++] = fd;
    }
    p[i] = -1;

    va_end(ap);

    r = pa_close_allv(p);
    pa_xfree(p);

    return r;
}

int pa_close_allv(const int except_fds[]) {
#ifndef OS_IS_WIN32
    struct rlimit rl;
    int maxfd, fd;

#if defined(__linux__) || defined(__sun)
    int saved_errno;
    DIR *d;

    if ((d = opendir("/proc/self/fd"))) {

        struct dirent *de;

        while ((de = readdir(d))) {
            bool found;
            long l;
            char *e = NULL;
            int i;

            if (de->d_name[0] == '.')
                continue;

            errno = 0;
            l = strtol(de->d_name, &e, 10);
            if (errno != 0 || !e || *e) {
                closedir(d);
                errno = EINVAL;
                return -1;
            }

            fd = (int) l;

            if ((long) fd != l) {
                closedir(d);
                errno = EINVAL;
                return -1;
            }

            if (fd < 3)
                continue;

            if (fd == dirfd(d))
                continue;

            found = false;
            for (i = 0; except_fds[i] >= 0; i++)
                if (except_fds[i] == fd) {
                    found = true;
                    break;
                }

            if (found)
                continue;

            if (pa_close(fd) < 0) {
                saved_errno = errno;
                closedir(d);
                errno = saved_errno;

                return -1;
            }
        }

        closedir(d);
        return 0;
    }

#endif

    if (getrlimit(RLIMIT_NOFILE, &rl) >= 0)
        maxfd = (int) rl.rlim_max;
    else
        maxfd = sysconf(_SC_OPEN_MAX);

    for (fd = 3; fd < maxfd; fd++) {
        int i;
        bool found;

        found = false;
        for (i = 0; except_fds[i] >= 0; i++)
            if (except_fds[i] == fd) {
                found = true;
                break;
            }

        if (found)
            continue;

        if (pa_close(fd) < 0 && errno != EBADF)
            return -1;
    }
#endif  /* !OS_IS_WIN32 */

    return 0;
}

int pa_unblock_sigs(int except, ...) {
    va_list ap;
    unsigned n = 0, i;
    int r, *p;

    va_start(ap, except);

    if (except >= 1)
        for (n = 1; va_arg(ap, int) >= 0; n++)
            ;

    va_end(ap);

    p = pa_xnew(int, n+1);

    va_start(ap, except);

    i = 0;
    if (except >= 1) {
        int sig;
        p[i++] = except;

        while ((sig = va_arg(ap, int)) >= 0)
            p[i++] = sig;
    }
    p[i] = -1;

    va_end(ap);

    r = pa_unblock_sigsv(p);
    pa_xfree(p);

    return r;
}

int pa_unblock_sigsv(const int except[]) {
#ifndef OS_IS_WIN32
    int i;
    sigset_t ss;

    if (sigemptyset(&ss) < 0)
        return -1;

    for (i = 0; except[i] > 0; i++)
        if (sigaddset(&ss, except[i]) < 0)
            return -1;

    return sigprocmask(SIG_SETMASK, &ss, NULL);
#else
    return 0;
#endif
}

int pa_reset_sigs(int except, ...) {
    va_list ap;
    unsigned n = 0, i;
    int *p, r;

    va_start(ap, except);

    if (except >= 1)
        for (n = 1; va_arg(ap, int) >= 0; n++)
            ;

    va_end(ap);

    p = pa_xnew(int, n+1);

    va_start(ap, except);

    i = 0;
    if (except >= 1) {
        int sig;
        p[i++] = except;

        while ((sig = va_arg(ap, int)) >= 0)
            p[i++] = sig;
    }
    p[i] = -1;

    va_end(ap);

    r = pa_reset_sigsv(p);
    pa_xfree(p);

    return r;
}

int pa_reset_sigsv(const int except[]) {
#ifndef OS_IS_WIN32
    int sig;

    for (sig = 1; sig < NSIG; sig++) {
        bool reset = true;

        switch (sig) {
            case SIGKILL:
            case SIGSTOP:
                reset = false;
                break;

            default: {
                int i;

                for (i = 0; except[i] > 0; i++) {
                    if (sig == except[i]) {
                        reset = false;
                        break;
                    }
                }
            }
        }

        if (reset) {
            struct sigaction sa;

            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = SIG_DFL;

            /* On Linux the first two RT signals are reserved by
             * glibc, and sigaction() will return EINVAL for them. */
            if ((sigaction(sig, &sa, NULL) < 0))
                if (errno != EINVAL)
                    return -1;
        }
    }
#endif

    return 0;
}

void pa_set_env(const char *key, const char *value) {
    pa_assert(key);
    pa_assert(value);

    /* This is not thread-safe */

#ifdef OS_IS_WIN32
    SetEnvironmentVariable(key, value);
#else
    setenv(key, value, 1);
#endif
}

void pa_unset_env(const char *key) {
    pa_assert(key);

    /* This is not thread-safe */

#ifdef OS_IS_WIN32
    SetEnvironmentVariable(key, NULL);
#else
    unsetenv(key);
#endif
}

void pa_set_env_and_record(const char *key, const char *value) {
    pa_assert(key);
    pa_assert(value);

    /* This is not thread-safe */

    pa_set_env(key, value);
    recorded_env = pa_strlist_prepend(recorded_env, key);
}

void pa_unset_env_recorded(void) {

    /* This is not thread-safe */

    for (;;) {
        char *s;

        recorded_env = pa_strlist_pop(recorded_env, &s);

        if (!s)
            break;

        pa_unset_env(s);
        pa_xfree(s);
    }
}

bool pa_in_system_mode(void) {
    const char *e;

    if (!(e = getenv("PULSE_SYSTEM")))
        return false;

    return !!atoi(e);
}

/* Checks a delimiters-separated list of words in haystack for needle */
bool pa_str_in_list(const char *haystack, const char *delimiters, const char *needle) {
    char *s;
    const char *state = NULL;

    if (!haystack || !needle)
        return false;

    while ((s = pa_split(haystack, delimiters, &state))) {
        if (pa_streq(needle, s)) {
            pa_xfree(s);
            return true;
        }

        pa_xfree(s);
    }

    return false;
}

/* Checks a whitespace-separated list of words in haystack for needle */
bool pa_str_in_list_spaces(const char *haystack, const char *needle) {
    const char *s;
    size_t n;
    const char *state = NULL;

    if (!haystack || !needle)
        return false;

    while ((s = pa_split_spaces_in_place(haystack, &n, &state))) {
        if (pa_strneq(needle, s, n))
            return true;
    }

    return false;
}

char* pa_str_strip_suffix(const char *str, const char *suffix) {
    size_t str_l, suf_l, prefix;
    char *ret;

    pa_assert(str);
    pa_assert(suffix);

    str_l = strlen(str);
    suf_l = strlen(suffix);

    if (str_l < suf_l)
        return NULL;

    prefix = str_l - suf_l;

    if (!pa_streq(&str[prefix], suffix))
        return NULL;

    ret = pa_xmalloc(prefix + 1);

    strncpy(ret, str, prefix);
    ret[prefix] = '\0';

    return ret;
}

char *pa_get_user_name_malloc(void) {
    ssize_t k;
    char *u;

#ifdef _SC_LOGIN_NAME_MAX
    k = (ssize_t) sysconf(_SC_LOGIN_NAME_MAX);

    if (k <= 0)
#endif
        k = 32;

    u = pa_xnew(char, k+1);

    if (!(pa_get_user_name(u, k))) {
        pa_xfree(u);
        return NULL;
    }

    return u;
}

char *pa_get_host_name_malloc(void) {
    size_t l;

    l = 100;
    for (;;) {
        char *c;

        c = pa_xmalloc(l);

        if (!pa_get_host_name(c, l)) {

            if (errno != EINVAL && errno != ENAMETOOLONG)
                break;

        } else if (strlen(c) < l-1) {
            char *u;

            if (*c == 0) {
                pa_xfree(c);
                break;
            }

            u = pa_utf8_filter(c);
            pa_xfree(c);
            return u;
        }

        /* Hmm, the hostname is as long the space we offered the
         * function, we cannot know if it fully fit in, so let's play
         * safe and retry. */

        pa_xfree(c);
        l *= 2;
    }

    return NULL;
}

char *pa_machine_id(void) {
    FILE *f;
    char *h;

    /* The returned value is supposed be some kind of ascii identifier
     * that is unique and stable across reboots. First we try if the machine-id
     * file is available. If it's available, that's great, since it provides an
     * identifier that suits our needs perfectly. If it's not, we fall back to
     * the hostname, which is not as good, since it can change over time. */

    /* We search for the machine-id file from four locations. The first two are
     * relative to the configured installation prefix, but if we're installed
     * under /usr/local, for example, it's likely that the machine-id won't be
     * found there, so we also try the hardcoded paths.
     *
     * PA_MACHINE_ID or PA_MACHINE_ID_FALLBACK might exist on a Windows system,
     * but the last two hardcoded paths certainly don't, hence we don't try
     * them on Windows. */
    if ((f = pa_fopen_cloexec(PA_MACHINE_ID, "r")) ||
        (f = pa_fopen_cloexec(PA_MACHINE_ID_FALLBACK, "r")) ||
#if !defined(OS_IS_WIN32)
        (f = pa_fopen_cloexec("/etc/machine-id", "r")) ||
        (f = pa_fopen_cloexec("/var/lib/dbus/machine-id", "r"))
#else
        false
#endif
        ) {
        char ln[34] = "", *r;

        r = fgets(ln, sizeof(ln)-1, f);
        fclose(f);

        pa_strip_nl(ln);

        if (r && ln[0])
            return pa_utf8_filter(ln);
    }

    if ((h = pa_get_host_name_malloc()))
        return h;

#if !defined(OS_IS_WIN32) && !defined(__ANDROID__)
    /* If no hostname was set we use the POSIX hostid. It's usually
     * the IPv4 address.  Might not be that stable. */
    return pa_sprintf_malloc("%08lx", (unsigned long) gethostid());
#else
    return NULL;
#endif
}

char *pa_session_id(void) {
    const char *e;

    e = getenv("XDG_SESSION_ID");
    if (!e)
        return NULL;

    return pa_utf8_filter(e);
}

char *pa_uname_string(void) {
#ifdef HAVE_UNAME
    struct utsname u;

    pa_assert_se(uname(&u) >= 0);

    return pa_sprintf_malloc("%s %s %s %s", u.sysname, u.machine, u.release, u.version);
#endif
#ifdef OS_IS_WIN32
    OSVERSIONINFO i;

    pa_zero(i);
    i.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    pa_assert_se(GetVersionEx(&i));

    return pa_sprintf_malloc("Windows %d.%d (%d) %s", i.dwMajorVersion, i.dwMinorVersion, i.dwBuildNumber, i.szCSDVersion);
#endif
}

#ifdef HAVE_VALGRIND_MEMCHECK_H
bool pa_in_valgrind(void) {
    static int b = 0;

    /* To make heisenbugs a bit simpler to find we check for $VALGRIND
     * here instead of really checking whether we run in valgrind or
     * not. */

    if (b < 1)
        b = getenv("VALGRIND") ? 2 : 1;

    return b > 1;
}
#endif

unsigned pa_gcd(unsigned a, unsigned b) {

    while (b > 0) {
        unsigned t = b;
        b = a % b;
        a = t;
    }

    return a;
}

void pa_reduce(unsigned *num, unsigned *den) {

    unsigned gcd = pa_gcd(*num, *den);

    if (gcd <= 0)
        return;

    *num /= gcd;
    *den /= gcd;

    pa_assert(pa_gcd(*num, *den) == 1);
}

unsigned pa_ncpus(void) {
    long ncpus;

#ifdef _SC_NPROCESSORS_ONLN
    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
#else
    ncpus = 1;
#endif

    return ncpus <= 0 ? 1 : (unsigned) ncpus;
}

char *pa_replace(const char*s, const char*a, const char *b) {
    pa_strbuf *sb;
    size_t an;

    pa_assert(s);
    pa_assert(a);
    pa_assert(*a);
    pa_assert(b);

    an = strlen(a);
    sb = pa_strbuf_new();

    for (;;) {
        const char *p;

        if (!(p = strstr(s, a)))
            break;

        pa_strbuf_putsn(sb, s, p-s);
        pa_strbuf_puts(sb, b);
        s = p + an;
    }

    pa_strbuf_puts(sb, s);

    return pa_strbuf_to_string_free(sb);
}

char *pa_escape(const char *p, const char *chars) {
    const char *s;
    const char *c;
    char *out_string, *output;
    int char_count = strlen(p);

    /* Maximum number of characters in output string
     * including trailing 0. */
    char_count = 2 * char_count + 1;

    /* allocate output string */
    out_string = pa_xmalloc(char_count);
    output = out_string;

    /* write output string */
    for (s = p; *s; ++s) {
        if (*s == '\\')
            *output++ = '\\';
        else if (chars) {
            for (c = chars; *c; ++c) {
                if (*s == *c) {
                    *output++ = '\\';
                    break;
                }
            }
        }
        *output++ = *s;
    }

    *output = 0;

    /* Remove trailing garbage */
    output = pa_xstrdup(out_string);

    pa_xfree(out_string);
    return output;
}

char *pa_unescape(char *p) {
    char *s, *d;
    bool escaped = false;

    for (s = p, d = p; *s; s++) {
        if (!escaped && *s == '\\') {
            escaped = true;
            continue;
        }

        *(d++) = *s;
        escaped = false;
    }

    *d = 0;

    return p;
}

char *pa_realpath(const char *path) {
    char *t;
    pa_assert(path);

    /* We want only absolute paths */
    if (path[0] != '/') {
        errno = EINVAL;
        return NULL;
    }

#if defined(__GLIBC__)
    {
        char *r;

        if (!(r = realpath(path, NULL)))
            return NULL;

        /* We copy this here in case our pa_xmalloc() is not
         * implemented on top of libc malloc() */
        t = pa_xstrdup(r);
        pa_xfree(r);
    }
#elif defined(PATH_MAX)
    {
        char *path_buf;
        path_buf = pa_xmalloc(PATH_MAX);

#if defined(OS_IS_WIN32)
        if (!(t = _fullpath(path_buf, path, _MAX_PATH))) {
            pa_xfree(path_buf);
            return NULL;
        }
#else
        if (!(t = realpath(path, path_buf))) {
            pa_xfree(path_buf);
            return NULL;
        }
#endif
    }
#else
#error "It's not clear whether this system supports realpath(..., NULL) like GNU libc does. If it doesn't we need a private version of realpath() here."
#endif

    return t;
}

void pa_disable_sigpipe(void) {

#ifdef SIGPIPE
    struct sigaction sa;

    pa_zero(sa);

    if (sigaction(SIGPIPE, NULL, &sa) < 0) {
        pa_log("sigaction(): %s", pa_cstrerror(errno));
        return;
    }

    sa.sa_handler = SIG_IGN;

    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        pa_log("sigaction(): %s", pa_cstrerror(errno));
        return;
    }
#endif
}

void pa_xfreev(void**a) {
    void **p;

    if (!a)
        return;

    for (p = a; *p; p++)
        pa_xfree(*p);

    pa_xfree(a);
}

char **pa_split_spaces_strv(const char *s) {
    char **t, *e;
    unsigned i = 0, n = 8;
    const char *state = NULL;

    t = pa_xnew(char*, n);
    while ((e = pa_split_spaces(s, &state))) {
        t[i++] = e;

        if (i >= n) {
            n *= 2;
            t = pa_xrenew(char*, t, n);
        }
    }

    if (i <= 0) {
        pa_xfree(t);
        return NULL;
    }

    t[i] = NULL;
    return t;
}

char* pa_maybe_prefix_path(const char *path, const char *prefix) {
    pa_assert(path);

    if (pa_is_path_absolute(path))
        return pa_xstrdup(path);

    return pa_sprintf_malloc("%s" PA_PATH_SEP "%s", prefix, path);
}

size_t pa_pipe_buf(int fd) {

#ifdef _PC_PIPE_BUF
    long n;

    if ((n = fpathconf(fd, _PC_PIPE_BUF)) >= 0)
        return (size_t) n;
#endif

#ifdef PIPE_BUF
    return PIPE_BUF;
#else
    return 4096;
#endif
}

void pa_reset_personality(void) {

#if defined(__linux__) && !defined(__ANDROID__)
    if (personality(PER_LINUX) < 0)
        pa_log_warn("Uh, personality() failed: %s", pa_cstrerror(errno));
#endif

}

bool pa_run_from_build_tree(void) {
    static bool b = false;

#ifdef HAVE_RUNNING_FROM_BUILD_TREE
    char *rp;
    PA_ONCE_BEGIN {
        if ((rp = pa_readlink("/proc/self/exe"))) {
            b = pa_startswith(rp, PA_BUILDDIR);
            pa_xfree(rp);
        }
    } PA_ONCE_END;
#endif

    return b;
}

const char *pa_get_temp_dir(void) {
    const char *t;

    if ((t = getenv("TMPDIR")) &&
        pa_is_path_absolute(t))
        return t;

    if ((t = getenv("TMP")) &&
        pa_is_path_absolute(t))
        return t;

    if ((t = getenv("TEMP")) &&
        pa_is_path_absolute(t))
        return t;

    if ((t = getenv("TEMPDIR")) &&
        pa_is_path_absolute(t))
        return t;

    return "/tmp";
}

int pa_open_cloexec(const char *fn, int flags, mode_t mode) {
    int fd;

#ifdef O_NOCTTY
    flags |= O_NOCTTY;
#endif

#ifdef O_CLOEXEC
    if ((fd = open(fn, flags|O_CLOEXEC, mode)) >= 0)
        goto finish;

    if (errno != EINVAL)
        return fd;
#endif

    if ((fd = open(fn, flags, mode)) >= 0)
        goto finish;

    /* return error */
    return fd;

finish:
    /* Some implementations might simply ignore O_CLOEXEC if it is not
     * understood, make sure FD_CLOEXEC is enabled anyway */

    pa_make_fd_cloexec(fd);
    return fd;
}

int pa_socket_cloexec(int domain, int type, int protocol) {
    int fd;

#ifdef SOCK_CLOEXEC
    if ((fd = socket(domain, type | SOCK_CLOEXEC, protocol)) >= 0)
        goto finish;

    if (errno != EINVAL)
        return fd;
#endif

    if ((fd = socket(domain, type, protocol)) >= 0)
        goto finish;

    /* return error */
    return fd;

finish:
    /* Some implementations might simply ignore SOCK_CLOEXEC if it is
     * not understood, make sure FD_CLOEXEC is enabled anyway */

    pa_make_fd_cloexec(fd);
    return fd;
}

int pa_pipe_cloexec(int pipefd[2]) {
    int r;

#ifdef HAVE_PIPE2
    if ((r = pipe2(pipefd, O_CLOEXEC)) >= 0)
        goto finish;

    if (errno == EMFILE) {
        pa_log_error("The per-process limit on the number of open file descriptors has been reached.");
        return r;
    }

    if (errno == ENFILE) {
        pa_log_error("The system-wide limit on the total number of open files has been reached.");
        return r;
    }

    if (errno != EINVAL && errno != ENOSYS)
        return r;

#endif

    if ((r = pipe(pipefd)) >= 0)
        goto finish;

    if (errno == EMFILE) {
        pa_log_error("The per-process limit on the number of open file descriptors has been reached.");
        return r;
    }

    if (errno == ENFILE) {
        pa_log_error("The system-wide limit on the total number of open files has been reached.");
        return r;
    }

    /* return error */
    return r;

finish:
    pa_make_fd_cloexec(pipefd[0]);
    pa_make_fd_cloexec(pipefd[1]);

    return 0;
}

int pa_accept_cloexec(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int fd;

    errno = 0;

#ifdef HAVE_ACCEPT4
    if ((fd = accept4(sockfd, addr, addrlen, SOCK_CLOEXEC)) >= 0)
        goto finish;

    if (errno != EINVAL && errno != ENOSYS)
        return fd;

#endif

#ifdef HAVE_PACCEPT
    if ((fd = paccept(sockfd, addr, addrlen, NULL, SOCK_CLOEXEC)) >= 0)
        goto finish;
#endif

    if ((fd = accept(sockfd, addr, addrlen)) >= 0)
        goto finish;

    /* return error */
    return fd;

finish:
    pa_make_fd_cloexec(fd);
    return fd;
}

FILE* pa_fopen_cloexec(const char *path, const char *mode) {
    FILE *f;
    char *m;

    m = pa_sprintf_malloc("%se", mode);

    errno = 0;
    if ((f = fopen(path, m))) {
        pa_xfree(m);
        goto finish;
    }

    pa_xfree(m);

    if (errno != EINVAL)
        return NULL;

    if (!(f = fopen(path, mode)))
        return NULL;

finish:
    pa_make_fd_cloexec(fileno(f));
    return f;
}

void pa_nullify_stdfds(void) {

#ifndef OS_IS_WIN32
        pa_close(STDIN_FILENO);
        pa_close(STDOUT_FILENO);
        pa_close(STDERR_FILENO);

        pa_assert_se(open("/dev/null", O_RDONLY) == STDIN_FILENO);
        pa_assert_se(open("/dev/null", O_WRONLY) == STDOUT_FILENO);
        pa_assert_se(open("/dev/null", O_WRONLY) == STDERR_FILENO);
#else
        FreeConsole();
#endif

}

char *pa_read_line_from_file(const char *fn) {
    FILE *f;
    char ln[256] = "", *r;

    if (!(f = pa_fopen_cloexec(fn, "r")))
        return NULL;

    r = fgets(ln, sizeof(ln)-1, f);
    fclose(f);

    if (!r) {
        errno = EIO;
        return NULL;
    }

    pa_strip_nl(ln);
    return pa_xstrdup(ln);
}

bool pa_running_in_vm(void) {

#if defined(__i386__) || defined(__x86_64__)

    /* Both CPUID and DMI are x86 specific interfaces... */

#ifdef HAVE_CPUID_H
    unsigned int eax, ebx, ecx, edx;
#endif

#ifdef __linux__
    const char *const dmi_vendors[] = {
        "/sys/class/dmi/id/sys_vendor",
        "/sys/class/dmi/id/board_vendor",
        "/sys/class/dmi/id/bios_vendor"
    };

    unsigned i;

    for (i = 0; i < PA_ELEMENTSOF(dmi_vendors); i++) {
        char *s;

        if ((s = pa_read_line_from_file(dmi_vendors[i]))) {

            if (pa_startswith(s, "QEMU") ||
                /* http://kb.vmware.com/selfservice/microsites/search.do?language=en_US&cmd=displayKC&externalId=1009458 */
                pa_startswith(s, "VMware") ||
                pa_startswith(s, "VMW") ||
                pa_startswith(s, "Microsoft Corporation") ||
                pa_startswith(s, "innotek GmbH") ||
                pa_startswith(s, "Xen")) {

                pa_xfree(s);
                return true;
            }

            pa_xfree(s);
        }
    }

#endif

#ifdef HAVE_CPUID_H

    /* Hypervisors provide presence on 0x1 cpuid leaf.
     * http://lwn.net/Articles/301888/ */
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0)
        return false;

    if (ecx & 0x80000000)
        return true;

#endif /* HAVE_CPUID_H */

#endif /* defined(__i386__) || defined(__x86_64__) */

    return false;
}

size_t pa_page_size(void) {
#if defined(PAGE_SIZE)
    return PAGE_SIZE;
#elif defined(PAGESIZE)
    return PAGESIZE;
#elif defined(HAVE_SYSCONF)
    static size_t page_size = 4096; /* Let's hope it's like x86. */

    PA_ONCE_BEGIN {
        long ret = sysconf(_SC_PAGE_SIZE);
        if (ret > 0)
            page_size = ret;
    } PA_ONCE_END;

    return page_size;
#else
    return 4096;
#endif
}
