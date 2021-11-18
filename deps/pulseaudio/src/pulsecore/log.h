#ifndef foologhfoo
#define foologhfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <stdarg.h>
#include <stdlib.h>

#include <pulsecore/macro.h>
#include <pulse/gccmacro.h>

/* A simple logging subsystem */

/* Where to log to */
typedef enum pa_log_target_type {
    PA_LOG_STDERR,      /* default */
    PA_LOG_SYSLOG,
#ifdef HAVE_SYSTEMD_JOURNAL
    PA_LOG_JOURNAL,     /* systemd journal */
#endif
    PA_LOG_NULL,        /* to /dev/null */
    PA_LOG_FILE,        /* to a user specified file */
    PA_LOG_NEWFILE,     /* with an automatic suffix to avoid overwriting anything */
} pa_log_target_type_t;

typedef enum pa_log_level {
    PA_LOG_ERROR  = 0,    /* Error messages */
    PA_LOG_WARN   = 1,    /* Warning messages */
    PA_LOG_NOTICE = 2,    /* Notice messages */
    PA_LOG_INFO   = 3,    /* Info messages */
    PA_LOG_DEBUG  = 4,    /* Debug messages */
    PA_LOG_LEVEL_MAX
} pa_log_level_t;

typedef enum pa_log_flags {
    PA_LOG_COLORS      = 0x01, /* Show colorful output */
    PA_LOG_PRINT_TIME  = 0x02, /* Show time */
    PA_LOG_PRINT_FILE  = 0x04, /* Show source file */
    PA_LOG_PRINT_META  = 0x08, /* Show extended location information */
    PA_LOG_PRINT_LEVEL = 0x10, /* Show log level prefix */
} pa_log_flags_t;

typedef enum pa_log_merge {
    PA_LOG_SET,
    PA_LOG_UNSET,
    PA_LOG_RESET
} pa_log_merge_t;

typedef struct {
    pa_log_target_type_t type;
    char *file;
} pa_log_target;

/* Set an identification for the current daemon. Used when logging to syslog. */
void pa_log_set_ident(const char *p);

/* Set a log target. */
int pa_log_set_target(pa_log_target *t);

/* Maximal log level */
void pa_log_set_level(pa_log_level_t l);

/* Set flags */
void pa_log_set_flags(pa_log_flags_t flags, pa_log_merge_t merge);

/* Enable backtrace */
void pa_log_set_show_backtrace(unsigned nlevels);

/* Skip the first backtrace frames */
void pa_log_set_skip_backtrace(unsigned nlevels);

void pa_log_level_meta(
        pa_log_level_t level,
        const char*file,
        int line,
        const char *func,
        const char *format, ...) PA_GCC_PRINTF_ATTR(5,6);

void pa_log_levelv_meta(
        pa_log_level_t level,
        const char*file,
        int line,
        const char *func,
        const char *format,
        va_list ap);

void pa_log_level(
        pa_log_level_t level,
        const char *format, ...) PA_GCC_PRINTF_ATTR(2,3);

void pa_log_levelv(
        pa_log_level_t level,
        const char *format,
        va_list ap);

pa_log_target *pa_log_target_new(pa_log_target_type_t type, const char *file);

void pa_log_target_free(pa_log_target *t);

pa_log_target *pa_log_parse_target(const char *string);

char *pa_log_target_to_string(const pa_log_target *t);

#if __STDC_VERSION__ >= 199901L

/* ISO varargs available */

#define pa_log_debug(...)  pa_log_level_meta(PA_LOG_DEBUG,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define pa_log_info(...)   pa_log_level_meta(PA_LOG_INFO,   __FILE__, __LINE__, __func__, __VA_ARGS__)
#define pa_log_notice(...) pa_log_level_meta(PA_LOG_NOTICE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define pa_log_warn(...)   pa_log_level_meta(PA_LOG_WARN,   __FILE__, __LINE__, __func__, __VA_ARGS__)
#define pa_log_error(...)  pa_log_level_meta(PA_LOG_ERROR,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define pa_logl(level, ...)  pa_log_level_meta(level,  __FILE__, __LINE__, __func__, __VA_ARGS__)

#else

#define LOG_FUNC(suffix, level) \
PA_GCC_UNUSED static void pa_log_##suffix(const char *format, ...) { \
    va_list ap; \
    va_start(ap, format); \
    pa_log_levelv_meta(level, NULL, 0, NULL, format, ap); \
    va_end(ap); \
}

LOG_FUNC(debug, PA_LOG_DEBUG)
LOG_FUNC(info, PA_LOG_INFO)
LOG_FUNC(notice, PA_LOG_NOTICE)
LOG_FUNC(warn, PA_LOG_WARN)
LOG_FUNC(error, PA_LOG_ERROR)

#endif

#define pa_log pa_log_error

bool pa_log_ratelimit(pa_log_level_t level);

#endif
