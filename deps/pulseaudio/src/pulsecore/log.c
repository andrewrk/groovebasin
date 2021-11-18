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

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#ifdef HAVE_SYSTEMD_JOURNAL

/* sd_journal_send() implicitly add fields for the source file,
 * function name and code line from where it's invoked. As the
 * correct code location fields CODE_FILE, CODE_LINE and
 * CODE_FUNC are already handled by this module, we do not want
 * the automatic values supplied by the systemd journal API.
 *
 * Without suppressing these, both the actual log event source
 * and the call to sd_journal_send() will be logged. */
#define SD_JOURNAL_SUPPRESS_LOCATION

#include <systemd/sd-journal.h>
#endif

#include <pulse/gccmacro.h>
#include <pulse/rtclock.h>
#include <pulse/utf8.h>
#include <pulse/xmalloc.h>
#include <pulse/util.h>
#include <pulse/timeval.h>

#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/once.h>
#include <pulsecore/ratelimit.h>
#include <pulsecore/thread.h>
#include <pulsecore/i18n.h>

#include "log.h"

#define ENV_LOG_SYSLOG "PULSE_LOG_SYSLOG"
#define ENV_LOG_JOURNAL "PULSE_LOG_JOURNAL"
#define ENV_LOG_LEVEL "PULSE_LOG"
#define ENV_LOG_COLORS "PULSE_LOG_COLORS"
#define ENV_LOG_PRINT_TIME "PULSE_LOG_TIME"
#define ENV_LOG_PRINT_FILE "PULSE_LOG_FILE"
#define ENV_LOG_PRINT_META "PULSE_LOG_META"
#define ENV_LOG_PRINT_LEVEL "PULSE_LOG_LEVEL"
#define ENV_LOG_BACKTRACE "PULSE_LOG_BACKTRACE"
#define ENV_LOG_BACKTRACE_SKIP "PULSE_LOG_BACKTRACE_SKIP"
#define ENV_LOG_NO_RATELIMIT "PULSE_LOG_NO_RATE_LIMIT"
#define LOG_MAX_SUFFIX_NUMBER 99

static char *ident = NULL; /* in local charset format */
static pa_log_target target = { PA_LOG_STDERR, NULL };
static pa_log_target_type_t target_override;
static bool target_override_set = false;
static pa_log_level_t maximum_level = PA_LOG_ERROR, maximum_level_override = PA_LOG_ERROR;
static unsigned show_backtrace = 0, show_backtrace_override = 0, skip_backtrace = 0;
static pa_log_flags_t flags = 0, flags_override = 0;
static bool no_rate_limit = false;
static int log_fd = -1;
static int write_type = 0;

#ifdef HAVE_SYSLOG_H
static const int level_to_syslog[] = {
    [PA_LOG_ERROR] = LOG_ERR,
    [PA_LOG_WARN] = LOG_WARNING,
    [PA_LOG_NOTICE] = LOG_NOTICE,
    [PA_LOG_INFO] = LOG_INFO,
    [PA_LOG_DEBUG] = LOG_DEBUG
};
#endif

/* These are actually equivalent to the syslog ones
 * but we don't want to depend on syslog.h */
#ifdef HAVE_SYSTEMD_JOURNAL
static const int level_to_journal[] = {
    [PA_LOG_ERROR]  = 3,
    [PA_LOG_WARN]   = 4,
    [PA_LOG_NOTICE] = 5,
    [PA_LOG_INFO]   = 6,
    [PA_LOG_DEBUG]  = 7
};
#endif

static const char level_to_char[] = {
    [PA_LOG_ERROR] = 'E',
    [PA_LOG_WARN] = 'W',
    [PA_LOG_NOTICE] = 'N',
    [PA_LOG_INFO] = 'I',
    [PA_LOG_DEBUG] = 'D'
};

void pa_log_set_ident(const char *p) {
    pa_xfree(ident);

    if (!(ident = pa_utf8_to_locale(p)))
        ident = pa_ascii_filter(p);
}

/* To make valgrind shut up. */
static void ident_destructor(void) PA_GCC_DESTRUCTOR;
static void ident_destructor(void) {
    if (!pa_in_valgrind())
        return;

    pa_xfree(ident);
}

void pa_log_set_level(pa_log_level_t l) {
    pa_assert(l < PA_LOG_LEVEL_MAX);

    maximum_level = l;
}

int pa_log_set_target(pa_log_target *t) {
    int fd = -1;
    int old_fd;

    pa_assert(t);

    switch (t->type) {
        case PA_LOG_STDERR:
        case PA_LOG_SYSLOG:
#ifdef HAVE_SYSTEMD_JOURNAL
        case PA_LOG_JOURNAL:
#endif
        case PA_LOG_NULL:
            break;
        case PA_LOG_FILE:
            if ((fd = pa_open_cloexec(t->file, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
                pa_log(_("Failed to open target file '%s'."), t->file);
                return -1;
            }
            break;
        case PA_LOG_NEWFILE: {
            char *file_path;
            char *p;
            unsigned version;

            file_path = pa_sprintf_malloc("%s.xx", t->file);
            p = file_path + strlen(t->file);

            for (version = 0; version <= LOG_MAX_SUFFIX_NUMBER; version++) {
                memset(p, 0, 3); /* Overwrite the ".xx" part in file_path with zero bytes. */

                if (version > 0)
                    pa_snprintf(p, 4, ".%u", version); /* Why 4? ".xx" + termitating zero byte. */

                if ((fd = pa_open_cloexec(file_path, O_WRONLY | O_TRUNC | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) >= 0)
                    break;
            }

            if (version > LOG_MAX_SUFFIX_NUMBER) {
                pa_log(_("Tried to open target file '%s', '%s.1', '%s.2' ... '%s.%d', but all failed."),
                        t->file, t->file, t->file, t->file, LOG_MAX_SUFFIX_NUMBER);
                pa_xfree(file_path);
                return -1;
            } else
                pa_log_debug("Opened target file %s\n", file_path);

            pa_xfree(file_path);
            break;
        }
    }

    target.type = t->type;
    pa_xfree(target.file);
    target.file = pa_xstrdup(t->file);

    old_fd = log_fd;
    log_fd = fd;

    if (old_fd >= 0)
        pa_close(old_fd);

    return 0;
}

void pa_log_set_flags(pa_log_flags_t _flags, pa_log_merge_t merge) {
    pa_assert(!(_flags & ~(PA_LOG_COLORS|PA_LOG_PRINT_TIME|PA_LOG_PRINT_FILE|PA_LOG_PRINT_META|PA_LOG_PRINT_LEVEL)));

    if (merge == PA_LOG_SET)
        flags |= _flags;
    else if (merge == PA_LOG_UNSET)
        flags &= ~_flags;
    else
        flags = _flags;
}

void pa_log_set_show_backtrace(unsigned nlevels) {
    show_backtrace = nlevels;
}

void pa_log_set_skip_backtrace(unsigned nlevels) {
    skip_backtrace = nlevels;
}

#ifdef HAVE_EXECINFO_H

static char* get_backtrace(unsigned show_nframes) {
    void* trace[32];
    int n_frames;
    char **symbols, *e, *r;
    unsigned j, n, s;
    size_t a;

    pa_assert(show_nframes > 0);

    n_frames = backtrace(trace, PA_ELEMENTSOF(trace));

    if (n_frames <= 0)
        return NULL;

    symbols = backtrace_symbols(trace, n_frames);

    if (!symbols)
        return NULL;

    s = skip_backtrace;
    n = PA_MIN((unsigned) n_frames, s + show_nframes);

    a = 4;

    for (j = s; j < n; j++) {
        if (j > s)
            a += 2;
        a += strlen(pa_path_get_filename(symbols[j]));
    }

    r = pa_xnew(char, a);

    strcpy(r, " (");
    e = r + 2;

    for (j = s; j < n; j++) {
        const char *sym;

        if (j > s) {
            strcpy(e, "<<");
            e += 2;
        }

        sym = pa_path_get_filename(symbols[j]);

        strcpy(e, sym);
        e += strlen(sym);
    }

    strcpy(e, ")");

    free(symbols);

    return r;
}

#endif

static void init_defaults(void) {
    PA_ONCE_BEGIN {

        const char *e;

        if (!ident) {
            char binary[256];
            if (pa_get_binary_name(binary, sizeof(binary)))
                pa_log_set_ident(binary);
        }

        if (getenv(ENV_LOG_SYSLOG)) {
            target_override = PA_LOG_SYSLOG;
            target_override_set = true;
        }

#ifdef HAVE_SYSTEMD_JOURNAL
        if (getenv(ENV_LOG_JOURNAL)) {
            target_override = PA_LOG_JOURNAL;
            target_override_set = true;
        }
#endif

        if ((e = getenv(ENV_LOG_LEVEL))) {
            maximum_level_override = (pa_log_level_t) atoi(e);

            if (maximum_level_override >= PA_LOG_LEVEL_MAX)
                maximum_level_override = PA_LOG_LEVEL_MAX-1;
        }

        if (getenv(ENV_LOG_COLORS))
            flags_override |= PA_LOG_COLORS;

        if (getenv(ENV_LOG_PRINT_TIME))
            flags_override |= PA_LOG_PRINT_TIME;

        if (getenv(ENV_LOG_PRINT_FILE))
            flags_override |= PA_LOG_PRINT_FILE;

        if (getenv(ENV_LOG_PRINT_META))
            flags_override |= PA_LOG_PRINT_META;

        if (getenv(ENV_LOG_PRINT_LEVEL))
            flags_override |= PA_LOG_PRINT_LEVEL;

        if ((e = getenv(ENV_LOG_BACKTRACE))) {
            show_backtrace_override = (unsigned) atoi(e);

            if (show_backtrace_override <= 0)
                show_backtrace_override = 0;
        }

        if ((e = getenv(ENV_LOG_BACKTRACE_SKIP))) {
            skip_backtrace = (unsigned) atoi(e);

            if (skip_backtrace <= 0)
                skip_backtrace = 0;
        }

        if (getenv(ENV_LOG_NO_RATELIMIT))
            no_rate_limit = true;

    } PA_ONCE_END;
}

#ifdef HAVE_SYSLOG_H
static void log_syslog(pa_log_level_t level, char *t, char *timestamp, char *location, char *bt) {
    char *local_t;

    openlog(ident, LOG_PID, LOG_USER);

    if ((local_t = pa_utf8_to_locale(t)))
        t = local_t;

    syslog(level_to_syslog[level], "%s%s%s%s", timestamp, location, t, pa_strempty(bt));
    pa_xfree(local_t);
}
#endif

void pa_log_levelv_meta(
        pa_log_level_t level,
        const char*file,
        int line,
        const char *func,
        const char *format,
        va_list ap) {

    char *t, *n;
    int saved_errno = errno;
    char *bt = NULL;
    pa_log_target_type_t _target;
    pa_log_level_t _maximum_level;
    unsigned _show_backtrace;
    pa_log_flags_t _flags;

    /* We don't use dynamic memory allocation here to minimize the hit
     * in RT threads */
    char text[16*1024], location[128], timestamp[32];

    pa_assert(level < PA_LOG_LEVEL_MAX);
    pa_assert(format);

    init_defaults();

    _target = target_override_set ? target_override : target.type;
    _maximum_level = PA_MAX(maximum_level, maximum_level_override);
    _show_backtrace = PA_MAX(show_backtrace, show_backtrace_override);
    _flags = flags | flags_override;

    if (PA_LIKELY(level > _maximum_level)) {
        errno = saved_errno;
        return;
    }

    pa_vsnprintf(text, sizeof(text), format, ap);

    if ((_flags & PA_LOG_PRINT_META) && file && line > 0 && func)
        pa_snprintf(location, sizeof(location), "[%s][%s:%i %s()] ",
                    pa_strnull(pa_thread_get_name(pa_thread_self())), file, line, func);
    else if ((_flags & (PA_LOG_PRINT_META|PA_LOG_PRINT_FILE)) && file)
        pa_snprintf(location, sizeof(location), "[%s] %s: ",
                    pa_strnull(pa_thread_get_name(pa_thread_self())), pa_path_get_filename(file));
    else
        location[0] = 0;

    if (_flags & PA_LOG_PRINT_TIME) {
        static pa_usec_t start, last;
        pa_usec_t u, a, r;

        u = pa_rtclock_now();

        PA_ONCE_BEGIN {
            start = u;
            last = u;
        } PA_ONCE_END;

        r = u - last;
        a = u - start;

        /* This is not thread safe, but this is a debugging tool only
         * anyway. */
        last = u;

        pa_snprintf(timestamp, sizeof(timestamp), "(%4llu.%03llu|%4llu.%03llu) ",
                    (unsigned long long) (a / PA_USEC_PER_SEC),
                    (unsigned long long) (((a / PA_USEC_PER_MSEC)) % 1000),
                    (unsigned long long) (r / PA_USEC_PER_SEC),
                    (unsigned long long) (((r / PA_USEC_PER_MSEC)) % 1000));

    } else
        timestamp[0] = 0;

#ifdef HAVE_EXECINFO_H
    if (_show_backtrace > 0)
        bt = get_backtrace(_show_backtrace);
#endif

    if (!pa_utf8_valid(text))
        pa_logl(level, "Invalid UTF-8 string following below:");

    for (t = text; t; t = n) {
        if ((n = strchr(t, '\n'))) {
            *n = 0;
            n++;
        }

        /* We ignore strings only made out of whitespace */
        if (t[strspn(t, "\t ")] == 0)
            continue;

        switch (_target) {

            case PA_LOG_STDERR: {
                const char *prefix = "", *suffix = "", *grey = "";
                char *local_t;

#ifndef OS_IS_WIN32
                /* Yes indeed. Useless, but fun! */
                if ((_flags & PA_LOG_COLORS) && isatty(STDERR_FILENO)) {
                    if (level <= PA_LOG_ERROR)
                        prefix = "\x1B[1;31m";
                    else if (level <= PA_LOG_WARN)
                        prefix = "\x1B[1m";

                    if (bt)
                        grey = "\x1B[2m";

                    if (grey[0] || prefix[0])
                        suffix = "\x1B[0m";
                }
#endif

                /* We shouldn't be using dynamic allocation here to
                 * minimize the hit in RT threads */
                if ((local_t = pa_utf8_to_locale(t)))
                    t = local_t;

                if (_flags & PA_LOG_PRINT_LEVEL)
                    fprintf(stderr, "%s%c: %s%s%s%s%s%s\n", timestamp, level_to_char[level], location, prefix, t, grey, pa_strempty(bt), suffix);
                else
                    fprintf(stderr, "%s%s%s%s%s%s%s\n", timestamp, location, prefix, t, grey, pa_strempty(bt), suffix);
#ifdef OS_IS_WIN32
                fflush(stderr);
#endif

                pa_xfree(local_t);

                break;
            }

#ifdef HAVE_SYSLOG_H
            case PA_LOG_SYSLOG:
                log_syslog(level, t, timestamp, location, bt);
                break;
#endif

#ifdef HAVE_SYSTEMD_JOURNAL
            case PA_LOG_JOURNAL:
                if (sd_journal_send("MESSAGE=%s", t,
                                "PRIORITY=%i", level_to_journal[level],
                                "CODE_FILE=%s", file,
                                "CODE_FUNC=%s", func,
                                "CODE_LINE=%d", line,
                                "PULSE_BACKTRACE=%s", pa_strempty(bt),
                                NULL) < 0) {
#ifdef HAVE_SYSLOG_H
                    pa_log_target new_target = { .type = PA_LOG_SYSLOG, .file = NULL };

                    syslog(level_to_syslog[PA_LOG_ERROR], "%s%s%s", timestamp, __FILE__,
                           "Error writing logs to the journal. Redirect log messages to syslog.");
                    log_syslog(level, t, timestamp, location, bt);
#else
                    pa_log_target new_target = { .type = PA_LOG_STDERR, .file = NULL };

                    saved_errno = errno;
                    fprintf(stderr, "%s\n", "Error writing logs to the journal. Redirect log messages to console.");
                    fprintf(stderr, "%s\n", t);
#endif
                    pa_log_set_target(&new_target);
                }
                break;
#endif

            case PA_LOG_FILE:
            case PA_LOG_NEWFILE: {
                char *local_t;

                if ((local_t = pa_utf8_to_locale(t)))
                    t = local_t;

                if (log_fd >= 0) {
                    char metadata[256];

                    if (_flags & PA_LOG_PRINT_LEVEL)
                        pa_snprintf(metadata, sizeof(metadata), "%s%c: %s", timestamp, level_to_char[level], location);
                    else
                        pa_snprintf(metadata, sizeof(metadata), "%s%s", timestamp, location);

                    if ((pa_write(log_fd, metadata, strlen(metadata), &write_type) < 0)
                            || (pa_write(log_fd, t, strlen(t), &write_type) < 0)
                            || (bt && pa_write(log_fd, bt, strlen(bt), &write_type) < 0)
                            || (pa_write(log_fd, "\n", 1, &write_type) < 0)) {
                        pa_log_target new_target = { .type = PA_LOG_STDERR, .file = NULL };
                        saved_errno = errno;
                        fprintf(stderr, "%s\n", "Error writing logs to a file descriptor. Redirect log messages to console.");
                        fprintf(stderr, "%s %s\n", metadata, t);
                        pa_log_set_target(&new_target);
                    }
                }

                pa_xfree(local_t);

                break;
            }
            case PA_LOG_NULL:
            default:
                break;
        }
    }

    pa_xfree(bt);
    errno = saved_errno;
}

void pa_log_level_meta(
        pa_log_level_t level,
        const char*file,
        int line,
        const char *func,
        const char *format, ...) {

    va_list ap;
    va_start(ap, format);
    pa_log_levelv_meta(level, file, line, func, format, ap);
    va_end(ap);
}

void pa_log_levelv(pa_log_level_t level, const char *format, va_list ap) {
    pa_log_levelv_meta(level, NULL, 0, NULL, format, ap);
}

void pa_log_level(pa_log_level_t level, const char *format, ...) {
    va_list ap;

    va_start(ap, format);
    pa_log_levelv_meta(level, NULL, 0, NULL, format, ap);
    va_end(ap);
}

bool pa_log_ratelimit(pa_log_level_t level) {
    /* Not more than 10 messages every 5s */
    static PA_DEFINE_RATELIMIT(ratelimit, 5 * PA_USEC_PER_SEC, 10);

    init_defaults();

    if (no_rate_limit)
        return true;

    return pa_ratelimit_test(&ratelimit, level);
}

pa_log_target *pa_log_target_new(pa_log_target_type_t type, const char *file) {
    pa_log_target *t = NULL;

    t = pa_xnew(pa_log_target, 1);

    t->type = type;
    t->file = pa_xstrdup(file);

    return t;
}

void pa_log_target_free(pa_log_target *t) {
    pa_assert(t);

    pa_xfree(t->file);
    pa_xfree(t);
}

pa_log_target *pa_log_parse_target(const char *string) {
    pa_log_target *t = NULL;

    pa_assert(string);

    if (pa_streq(string, "stderr"))
        t = pa_log_target_new(PA_LOG_STDERR, NULL);
    else if (pa_streq(string, "syslog"))
        t = pa_log_target_new(PA_LOG_SYSLOG, NULL);
#ifdef HAVE_SYSTEMD_JOURNAL
    else if (pa_streq(string, "journal"))
        t = pa_log_target_new(PA_LOG_JOURNAL, NULL);
#endif
    else if (pa_streq(string, "null"))
        t = pa_log_target_new(PA_LOG_NULL, NULL);
    else if (pa_startswith(string, "file:"))
        t = pa_log_target_new(PA_LOG_FILE, string + 5);
    else if (pa_startswith(string, "newfile:"))
        t = pa_log_target_new(PA_LOG_NEWFILE, string + 8);
    else
        pa_log(_("Invalid log target."));

    return t;
}

char *pa_log_target_to_string(const pa_log_target *t) {
    char *string = NULL;

    pa_assert(t);

    switch (t->type) {
        case PA_LOG_STDERR:
            string = pa_xstrdup("stderr");
            break;
        case PA_LOG_SYSLOG:
            string = pa_xstrdup("syslog");
            break;
#ifdef HAVE_SYSTEMD_JOURNAL
        case PA_LOG_JOURNAL:
            string = pa_xstrdup("journal");
            break;
#endif
        case PA_LOG_NULL:
            string = pa_xstrdup("null");
            break;
        case PA_LOG_FILE:
            string = pa_sprintf_malloc("file:%s", t->file);
            break;
        case PA_LOG_NEWFILE:
            string = pa_sprintf_malloc("newfile:%s", t->file);
            break;
    }

    return string;
}
