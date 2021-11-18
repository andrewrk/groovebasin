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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/xmalloc.h>

#include <pulsecore/socket.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/refcnt.h>

#include "ioline.h"

#define BUFFER_LIMIT (64*1024)
#define READ_SIZE (1024)

struct pa_ioline {
    PA_REFCNT_DECLARE;

    pa_iochannel *io;
    pa_defer_event *defer_event;
    pa_mainloop_api *mainloop;

    char *wbuf;
    size_t wbuf_length, wbuf_index, wbuf_valid_length;

    char *rbuf;
    size_t rbuf_length, rbuf_index, rbuf_valid_length;

    pa_ioline_cb_t callback;
    void *userdata;

    pa_ioline_drain_cb_t drain_callback;
    void *drain_userdata;

    bool dead:1;
    bool defer_close:1;
};

static void io_callback(pa_iochannel*io, void *userdata);
static void defer_callback(pa_mainloop_api*m, pa_defer_event*e, void *userdata);

pa_ioline* pa_ioline_new(pa_iochannel *io) {
    pa_ioline *l;
    pa_assert(io);

    l = pa_xnew(pa_ioline, 1);
    PA_REFCNT_INIT(l);
    l->io = io;

    l->wbuf = NULL;
    l->wbuf_length = l->wbuf_index = l->wbuf_valid_length = 0;

    l->rbuf = NULL;
    l->rbuf_length = l->rbuf_index = l->rbuf_valid_length = 0;

    l->callback = NULL;
    l->userdata = NULL;

    l->drain_callback = NULL;
    l->drain_userdata = NULL;

    l->mainloop = pa_iochannel_get_mainloop_api(io);

    l->defer_event = l->mainloop->defer_new(l->mainloop, defer_callback, l);
    l->mainloop->defer_enable(l->defer_event, 0);

    l->dead = false;
    l->defer_close = false;

    pa_iochannel_set_callback(io, io_callback, l);

    return l;
}

static void ioline_free(pa_ioline *l) {
    pa_assert(l);

    if (l->io)
        pa_iochannel_free(l->io);

    if (l->defer_event)
        l->mainloop->defer_free(l->defer_event);

    pa_xfree(l->wbuf);
    pa_xfree(l->rbuf);
    pa_xfree(l);
}

void pa_ioline_unref(pa_ioline *l) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    if (PA_REFCNT_DEC(l) <= 0)
        ioline_free(l);
}

pa_ioline* pa_ioline_ref(pa_ioline *l) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    PA_REFCNT_INC(l);
    return l;
}

void pa_ioline_close(pa_ioline *l) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    l->dead = true;

    if (l->io) {
        pa_iochannel_free(l->io);
        l->io = NULL;
    }

    if (l->defer_event) {
        l->mainloop->defer_free(l->defer_event);
        l->defer_event = NULL;
    }

    if (l->callback)
        l->callback = NULL;
}

void pa_ioline_puts(pa_ioline *l, const char *c) {
    size_t len;

    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);
    pa_assert(c);

    if (l->dead)
        return;

    len = strlen(c);
    if (len > BUFFER_LIMIT - l->wbuf_valid_length)
        len = BUFFER_LIMIT - l->wbuf_valid_length;

    if (len) {
        pa_assert(l->wbuf_length >= l->wbuf_valid_length);

        /* In case the allocated buffer is too small, enlarge it. */
        if (l->wbuf_valid_length + len > l->wbuf_length) {
            size_t n = l->wbuf_valid_length+len;
            char *new = pa_xnew(char, (unsigned) n);

            if (l->wbuf) {
                memcpy(new, l->wbuf+l->wbuf_index, l->wbuf_valid_length);
                pa_xfree(l->wbuf);
            }

            l->wbuf = new;
            l->wbuf_length = n;
            l->wbuf_index = 0;
        } else if (l->wbuf_index + l->wbuf_valid_length + len > l->wbuf_length) {

            /* In case the allocated buffer fits, but the current index is too far from the start, move it to the front. */
            memmove(l->wbuf, l->wbuf+l->wbuf_index, l->wbuf_valid_length);
            l->wbuf_index = 0;
        }

        pa_assert(l->wbuf_index + l->wbuf_valid_length + len <= l->wbuf_length);

        /* Append the new string */
        memcpy(l->wbuf + l->wbuf_index + l->wbuf_valid_length, c, len);
        l->wbuf_valid_length += len;

        l->mainloop->defer_enable(l->defer_event, 1);
    }
}

void pa_ioline_set_callback(pa_ioline*l, pa_ioline_cb_t callback, void *userdata) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    if (l->dead)
        return;

    l->callback = callback;
    l->userdata = userdata;
}

void pa_ioline_set_drain_callback(pa_ioline*l, pa_ioline_drain_cb_t callback, void *userdata) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    if (l->dead)
        return;

    l->drain_callback = callback;
    l->drain_userdata = userdata;
}

static void failure(pa_ioline *l, bool process_leftover) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);
    pa_assert(!l->dead);

    if (process_leftover && l->rbuf_valid_length > 0) {
        /* Pass the last missing bit to the client */

        if (l->callback) {
            char *p = pa_xstrndup(l->rbuf+l->rbuf_index, l->rbuf_valid_length);
            l->callback(l, p, l->userdata);
            pa_xfree(p);
        }
    }

    if (l->callback) {
        l->callback(l, NULL, l->userdata);
        l->callback = NULL;
    }

    pa_ioline_close(l);
}

static void scan_for_lines(pa_ioline *l, size_t skip) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);
    pa_assert(skip < l->rbuf_valid_length);

    while (!l->dead && l->rbuf_valid_length > skip) {
        char *e, *p;
        size_t m;

        if (!(e = memchr(l->rbuf + l->rbuf_index + skip, '\n', l->rbuf_valid_length - skip)))
            break;

        *e = 0;

        p = l->rbuf + l->rbuf_index;
        m = strlen(p);

        l->rbuf_index += m+1;
        l->rbuf_valid_length -= m+1;

        /* A shortcut for the next time */
        if (l->rbuf_valid_length == 0)
            l->rbuf_index = 0;

        if (l->callback)
            l->callback(l, pa_strip_nl(p), l->userdata);

        skip = 0;
    }

    /* If the buffer became too large and still no newline was found, drop it. */
    if (l->rbuf_valid_length >= BUFFER_LIMIT)
        l->rbuf_index = l->rbuf_valid_length = 0;
}

static int do_write(pa_ioline *l);

static int do_read(pa_ioline *l) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    while (l->io && !l->dead && pa_iochannel_is_readable(l->io)) {
        ssize_t r;
        size_t len;

        len = l->rbuf_length - l->rbuf_index - l->rbuf_valid_length;

        /* Check if we have to enlarge the read buffer */
        if (len < READ_SIZE) {
            size_t n = l->rbuf_valid_length+READ_SIZE;

            if (n >= BUFFER_LIMIT)
                n = BUFFER_LIMIT;

            if (l->rbuf_length >= n) {
                /* The current buffer is large enough, let's just move the data to the front */
                if (l->rbuf_valid_length)
                    memmove(l->rbuf, l->rbuf+l->rbuf_index, l->rbuf_valid_length);
            } else {
                /* Enlarge the buffer */
                char *new = pa_xnew(char, (unsigned) n);
                if (l->rbuf_valid_length)
                    memcpy(new, l->rbuf+l->rbuf_index, l->rbuf_valid_length);
                pa_xfree(l->rbuf);
                l->rbuf = new;
                l->rbuf_length = n;
            }

            l->rbuf_index = 0;
        }

        len = l->rbuf_length - l->rbuf_index - l->rbuf_valid_length;

        pa_assert(len >= READ_SIZE);

        /* Read some data */
        if ((r = pa_iochannel_read(l->io, l->rbuf+l->rbuf_index+l->rbuf_valid_length, len)) <= 0) {

            if (r < 0 && errno == EAGAIN)
                return 0;

            if (r < 0 && errno != ECONNRESET) {
                pa_log("read(): %s", pa_cstrerror(errno));
                failure(l, false);
            } else
                failure(l, true);

            return -1;
        }

        l->rbuf_valid_length += (size_t) r;

        /* Look if a line has been terminated in the newly read data */
        scan_for_lines(l, l->rbuf_valid_length - (size_t) r);
    }

    return 0;
}

/* Try to flush the buffer */
static int do_write(pa_ioline *l) {
    ssize_t r;

    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    while (l->io && !l->dead && pa_iochannel_is_writable(l->io) && l->wbuf_valid_length > 0) {

        if ((r = pa_iochannel_write(l->io, l->wbuf+l->wbuf_index, l->wbuf_valid_length)) < 0) {

            if (errno != EPIPE)
                pa_log("write(): %s", pa_cstrerror(errno));

            failure(l, false);

            return -1;
        }

        l->wbuf_index += (size_t) r;
        l->wbuf_valid_length -= (size_t) r;

        /* A shortcut for the next time */
        if (l->wbuf_valid_length == 0)
            l->wbuf_index = 0;
    }

    if (l->wbuf_valid_length <= 0 && l->drain_callback)
        l->drain_callback(l, l->drain_userdata);

    return 0;
}

/* Try to flush read/write data */
static void do_work(pa_ioline *l) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    pa_ioline_ref(l);

    l->mainloop->defer_enable(l->defer_event, 0);

    if (!l->dead)
        do_read(l);

    if (!l->dead)
        do_write(l);

    if (l->defer_close && !l->wbuf_valid_length)
        failure(l, true);

    pa_ioline_unref(l);
}

static void io_callback(pa_iochannel*io, void *userdata) {
    pa_ioline *l = userdata;

    pa_assert(io);
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    do_work(l);
}

static void defer_callback(pa_mainloop_api*m, pa_defer_event*e, void *userdata) {
    pa_ioline *l = userdata;

    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);
    pa_assert(l->mainloop == m);
    pa_assert(l->defer_event == e);

    do_work(l);
}

void pa_ioline_defer_close(pa_ioline *l) {
    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    l->defer_close = true;

    if (!l->wbuf_valid_length)
        l->mainloop->defer_enable(l->defer_event, 1);
}

void pa_ioline_printf(pa_ioline *l, const char *format, ...) {
    char *t;
    va_list ap;

    pa_assert(l);
    pa_assert(PA_REFCNT_VALUE(l) >= 1);

    va_start(ap, format);
    t = pa_vsprintf_malloc(format, ap);
    va_end(ap);

    pa_ioline_puts(l, t);
    pa_xfree(t);
}

pa_iochannel* pa_ioline_detach_iochannel(pa_ioline *l) {
    pa_iochannel *r;

    pa_assert(l);

    if (!l->io)
        return NULL;

    r = l->io;
    l->io = NULL;

    pa_iochannel_set_callback(r, NULL, NULL);

    return r;
}

bool pa_ioline_is_drained(pa_ioline *l) {
    pa_assert(l);

    return l->wbuf_valid_length <= 0;
}
