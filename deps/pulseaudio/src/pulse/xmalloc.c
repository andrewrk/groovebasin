/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <pulse/gccmacro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

#include "xmalloc.h"

/* Make sure not to allocate more than this much memory. */
#define MAX_ALLOC_SIZE (1024*1024*96) /* 96MB */

/* #undef malloc */
/* #undef free */
/* #undef realloc */
/* #undef strndup */
/* #undef strdup */

static void oom(void) PA_GCC_NORETURN;

/* called in case of an OOM situation. Prints an error message and
 * exits */
static void oom(void) {
    static const char e[] = "Not enough memory\n";
    pa_loop_write(STDERR_FILENO, e, sizeof(e)-1, NULL);
#ifdef SIGQUIT
    raise(SIGQUIT);
#endif
    _exit(1);
}

void* pa_xmalloc(size_t size) {
    void *p;
    pa_assert(size > 0);
    pa_assert(size < MAX_ALLOC_SIZE);

    if (!(p = malloc(size)))
        oom();

    return p;
}

void* pa_xmalloc0(size_t size) {
    void *p;
    pa_assert(size > 0);
    pa_assert(size < MAX_ALLOC_SIZE);

    if (!(p = calloc(1, size)))
        oom();

    return p;
}

void *pa_xrealloc(void *ptr, size_t size) {
    void *p;
    pa_assert(size > 0);
    pa_assert(size < MAX_ALLOC_SIZE);

    if (!(p = realloc(ptr, size)))
        oom();
    return p;
}

void* pa_xmemdup(const void *p, size_t l) {
    if (!p)
        return NULL;
    else {
        char *r = pa_xmalloc(l);
        memcpy(r, p, l);
        return r;
    }
}

char *pa_xstrdup(const char *s) {
    if (!s)
        return NULL;

    return pa_xmemdup(s, strlen(s)+1);
}

char *pa_xstrndup(const char *s, size_t l) {
    char *e, *r;

    if (!s)
        return NULL;

    if ((e = memchr(s, 0, l)))
        return pa_xmemdup(s, (size_t) (e-s+1));

    r = pa_xmalloc(l+1);
    memcpy(r, s, l);
    r[l] = 0;
    return r;
}

void pa_xfree(void *p) {
    int saved_errno;

    if (!p)
        return;

    saved_errno = errno;
    free(p);
    errno = saved_errno;
}
