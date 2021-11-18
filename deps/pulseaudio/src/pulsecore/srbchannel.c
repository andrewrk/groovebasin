/***
  This file is part of PulseAudio.

  Copyright 2014 David Henningsson, Canonical Ltd.

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

#include "srbchannel.h"

#include <pulsecore/atomic.h>
#include <pulse/xmalloc.h>

/* #define DEBUG_SRBCHANNEL */

/* This ringbuffer might be useful in other contexts too, but
 * right now it's only used inside the srbchannel, so let's keep it here
 * for the time being. */
typedef struct pa_ringbuffer pa_ringbuffer;

struct pa_ringbuffer {
    pa_atomic_t *count; /* amount of data in the buffer */
    int capacity;
    uint8_t *memory;
    int readindex, writeindex;
};

static void *pa_ringbuffer_peek(pa_ringbuffer *r, int *count) {
    int c = pa_atomic_load(r->count);

    if (r->readindex + c > r->capacity)
        *count = r->capacity - r->readindex;
    else
        *count = c;

    return r->memory + r->readindex;
}

/* Returns true only if the buffer was completely full before the drop. */
static bool pa_ringbuffer_drop(pa_ringbuffer *r, int count) {
    bool b = pa_atomic_sub(r->count, count) >= r->capacity;

    r->readindex += count;
    r->readindex %= r->capacity;

    return b;
}

static void *pa_ringbuffer_begin_write(pa_ringbuffer *r, int *count) {
    int c = pa_atomic_load(r->count);

    *count = PA_MIN(r->capacity - r->writeindex, r->capacity - c);

    return r->memory + r->writeindex;
}

static void pa_ringbuffer_end_write(pa_ringbuffer *r, int count) {
    pa_atomic_add(r->count, count);
    r->writeindex += count;
    r->writeindex %= r->capacity;
}

struct pa_srbchannel {
    pa_ringbuffer rb_read, rb_write;
    pa_fdsem *sem_read, *sem_write;
    pa_memblock *memblock;

    void *cb_userdata;
    pa_srbchannel_cb_t callback;

    pa_io_event *read_event;
    pa_defer_event *defer_event;
    pa_mainloop_api *mainloop;
};

/* We always listen to sem_read, and always signal on sem_write.
 *
 * This means we signal the same semaphore for two scenarios:
 * 1) We have written something to our send buffer, and want the other
 *    side to read it
 * 2) We have read something from our receive buffer that was previously
 *    completely full, and want the other side to continue writing
*/

size_t pa_srbchannel_write(pa_srbchannel *sr, const void *data, size_t l) {
    size_t written = 0;

    while (l > 0) {
        int towrite;
        void *ptr = pa_ringbuffer_begin_write(&sr->rb_write, &towrite);

        if ((size_t) towrite > l)
            towrite = l;

        if (towrite == 0) {
#ifdef DEBUG_SRBCHANNEL
            pa_log("srbchannel output buffer full");
#endif
            break;
        }

        memcpy(ptr, data, towrite);
        pa_ringbuffer_end_write(&sr->rb_write, towrite);
        written += towrite;
        data = (uint8_t*) data + towrite;
        l -= towrite;
    }
#ifdef DEBUG_SRBCHANNEL
    pa_log("Wrote %d bytes to srbchannel, signalling fdsem", (int) written);
#endif

    pa_fdsem_post(sr->sem_write);
    return written;
}

size_t pa_srbchannel_read(pa_srbchannel *sr, void *data, size_t l) {
    size_t isread = 0;

    while (l > 0) {
        int toread;
        void *ptr = pa_ringbuffer_peek(&sr->rb_read, &toread);

        if ((size_t) toread > l)
            toread = l;

        if (toread == 0)
            break;

        memcpy(data, ptr, toread);

        if (pa_ringbuffer_drop(&sr->rb_read, toread)) {
#ifdef DEBUG_SRBCHANNEL
            pa_log("Read from full output buffer, signalling fdsem");
#endif
            pa_fdsem_post(sr->sem_write);
        }

        isread += toread;
        data = (uint8_t*) data + toread;
        l -= toread;
    }

#ifdef DEBUG_SRBCHANNEL
    pa_log("Read %d bytes from srbchannel", (int) isread);
#endif

    return isread;
}

/* This is the memory layout of the ringbuffer shm block. It is followed by
   read and write ringbuffer memory. */
struct srbheader {
    pa_atomic_t read_count;
    pa_atomic_t write_count;

    pa_fdsem_data read_semdata;
    pa_fdsem_data write_semdata;

    int capacity;
    int readbuf_offset;
    int writebuf_offset;

    /* TODO: Maybe a marker here to make sure we talk to a server with equally sized struct */
};

static void srbchannel_rwloop(pa_srbchannel* sr) {
    do {
#ifdef DEBUG_SRBCHANNEL
        int q;
        pa_ringbuffer_peek(&sr->rb_read, &q);
        pa_log("In rw loop from srbchannel, before callback, count = %d", q);
#endif

        if (sr->callback) {
            if (!sr->callback(sr, sr->cb_userdata)) {
#ifdef DEBUG_SRBCHANNEL
                pa_log("Aborting read loop from srbchannel");
#endif
                return;
            }
        }

#ifdef DEBUG_SRBCHANNEL
        pa_ringbuffer_peek(&sr->rb_read, &q);
        pa_log("In rw loop from srbchannel, after callback, count = %d", q);
#endif

    } while (pa_fdsem_before_poll(sr->sem_read) < 0);
}

static void semread_cb(pa_mainloop_api *m, pa_io_event *e, int fd, pa_io_event_flags_t events, void *userdata) {
    pa_srbchannel* sr = userdata;

    pa_fdsem_after_poll(sr->sem_read);
    srbchannel_rwloop(sr);
}

static void defer_cb(pa_mainloop_api *m, pa_defer_event *e, void *userdata) {
    pa_srbchannel* sr = userdata;

#ifdef DEBUG_SRBCHANNEL
    pa_log("Calling rw loop from deferred event");
#endif

    m->defer_enable(e, 0);
    srbchannel_rwloop(sr);
}

pa_srbchannel* pa_srbchannel_new(pa_mainloop_api *m, pa_mempool *p) {
    int capacity;
    int readfd;
    struct srbheader *srh;

    pa_srbchannel* sr = pa_xmalloc0(sizeof(pa_srbchannel));
    sr->mainloop = m;
    sr->memblock = pa_memblock_new_pool(p, -1);
    if (!sr->memblock)
        goto fail;

    srh = pa_memblock_acquire(sr->memblock);
    pa_zero(*srh);

    sr->rb_read.memory = (uint8_t*) srh + PA_ALIGN(sizeof(*srh));
    srh->readbuf_offset = sr->rb_read.memory - (uint8_t*) srh;

    capacity = (pa_memblock_get_length(sr->memblock) - srh->readbuf_offset) / 2;

    sr->rb_write.memory = PA_ALIGN_PTR(sr->rb_read.memory + capacity);
    srh->writebuf_offset = sr->rb_write.memory - (uint8_t*) srh;

    capacity = PA_MIN(capacity, srh->writebuf_offset - srh->readbuf_offset);

    pa_log_debug("SHM block is %d bytes, ringbuffer capacity is 2 * %d bytes",
        (int) pa_memblock_get_length(sr->memblock), capacity);

    srh->capacity = sr->rb_read.capacity = sr->rb_write.capacity = capacity;

    sr->rb_read.count = &srh->read_count;
    sr->rb_write.count = &srh->write_count;

    sr->sem_read = pa_fdsem_new_shm(&srh->read_semdata);
    if (!sr->sem_read)
        goto fail;

    sr->sem_write = pa_fdsem_new_shm(&srh->write_semdata);
    if (!sr->sem_write)
        goto fail;

    readfd = pa_fdsem_get(sr->sem_read);

#ifdef DEBUG_SRBCHANNEL
    pa_log("Enabling io event on fd %d", readfd);
#endif

    sr->read_event = m->io_new(m, readfd, PA_IO_EVENT_INPUT, semread_cb, sr);
    m->io_enable(sr->read_event, PA_IO_EVENT_INPUT);

    return sr;

fail:
    pa_srbchannel_free(sr);

    return NULL;
}

static void pa_srbchannel_swap(pa_srbchannel *sr) {
    pa_srbchannel temp = *sr;

    sr->sem_read = temp.sem_write;
    sr->sem_write = temp.sem_read;
    sr->rb_read = temp.rb_write;
    sr->rb_write = temp.rb_read;
}

pa_srbchannel* pa_srbchannel_new_from_template(pa_mainloop_api *m, pa_srbchannel_template *t)
{
    int temp;
    struct srbheader *srh;
    pa_srbchannel* sr = pa_xmalloc0(sizeof(pa_srbchannel));

    sr->mainloop = m;
    sr->memblock = t->memblock;
    pa_memblock_ref(sr->memblock);
    srh = pa_memblock_acquire(sr->memblock);

    sr->rb_read.capacity = sr->rb_write.capacity = srh->capacity;
    sr->rb_read.count = &srh->read_count;
    sr->rb_write.count = &srh->write_count;

    sr->rb_read.memory = (uint8_t*) srh + srh->readbuf_offset;
    sr->rb_write.memory = (uint8_t*) srh + srh->writebuf_offset;

    sr->sem_read = pa_fdsem_open_shm(&srh->read_semdata, t->readfd);
    if (!sr->sem_read)
        goto fail;

    sr->sem_write = pa_fdsem_open_shm(&srh->write_semdata, t->writefd);
    if (!sr->sem_write)
        goto fail;

    pa_srbchannel_swap(sr);
    temp = t->readfd; t->readfd = t->writefd; t->writefd = temp;

#ifdef DEBUG_SRBCHANNEL
    pa_log("Enabling io event on fd %d", t->readfd);
#endif

    sr->read_event = m->io_new(m, t->readfd, PA_IO_EVENT_INPUT, semread_cb, sr);
    m->io_enable(sr->read_event, PA_IO_EVENT_INPUT);

    return sr;

fail:
    pa_srbchannel_free(sr);

    return NULL;
}

void pa_srbchannel_export(pa_srbchannel *sr, pa_srbchannel_template *t) {
    t->memblock = sr->memblock;
    t->readfd = pa_fdsem_get(sr->sem_read);
    t->writefd = pa_fdsem_get(sr->sem_write);
}

void pa_srbchannel_set_callback(pa_srbchannel *sr, pa_srbchannel_cb_t callback, void *userdata) {
    if (sr->callback)
        pa_fdsem_after_poll(sr->sem_read);

    sr->callback = callback;
    sr->cb_userdata = userdata;

    if (sr->callback) {
        /* If there are events to be read already in the ringbuffer, we will not get any IO event for that,
           because that's how pa_fdsem works. Therefore check the ringbuffer in a defer event instead. */
        if (!sr->defer_event)
            sr->defer_event = sr->mainloop->defer_new(sr->mainloop, defer_cb, sr);
        sr->mainloop->defer_enable(sr->defer_event, 1);
    }
}

void pa_srbchannel_free(pa_srbchannel *sr)
{
#ifdef DEBUG_SRBCHANNEL
    pa_log("Freeing srbchannel");
#endif
    pa_assert(sr);

    if (sr->defer_event)
        sr->mainloop->defer_free(sr->defer_event);
    if (sr->read_event)
        sr->mainloop->io_free(sr->read_event);

    if (sr->sem_read)
        pa_fdsem_free(sr->sem_read);
    if (sr->sem_write)
        pa_fdsem_free(sr->sem_write);

    if (sr->memblock) {
        pa_memblock_release(sr->memblock);
        pa_memblock_unref(sr->memblock);
    }

    pa_xfree(sr);
}
