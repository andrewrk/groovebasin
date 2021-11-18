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

#include <unistd.h>
#include <errno.h>

#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>
#include <pulsecore/log.h>
#include <pulsecore/semaphore.h>
#include <pulsecore/macro.h>
#include <pulsecore/mutex.h>
#include <pulsecore/flist.h>

#include "asyncmsgq.h"

PA_STATIC_FLIST_DECLARE(asyncmsgq, 0, pa_xfree);
PA_STATIC_FLIST_DECLARE(semaphores, 0, (void(*)(void*)) pa_semaphore_free);

struct asyncmsgq_item {
    int code;
    pa_msgobject *object;
    void *userdata;
    pa_free_cb_t free_cb;
    int64_t offset;
    pa_memchunk memchunk;
    pa_semaphore *semaphore;
    int ret;
};

struct pa_asyncmsgq {
    PA_REFCNT_DECLARE;
    pa_asyncq *asyncq;
    pa_mutex *mutex; /* only for the writer side */

    struct asyncmsgq_item *current;
};

pa_asyncmsgq *pa_asyncmsgq_new(unsigned size) {
    pa_asyncq *asyncq;
    pa_asyncmsgq *a;

    asyncq = pa_asyncq_new(size);
    if (!asyncq)
        return NULL;

    a = pa_xnew(pa_asyncmsgq, 1);

    PA_REFCNT_INIT(a);
    a->asyncq = asyncq;
    pa_assert_se(a->mutex = pa_mutex_new(false, true));
    a->current = NULL;

    return a;
}

static void asyncmsgq_free(pa_asyncmsgq *a) {
    struct asyncmsgq_item *i;
    pa_assert(a);

    while ((i = pa_asyncq_pop(a->asyncq, false))) {

        pa_assert(!i->semaphore);

        if (i->object)
            pa_msgobject_unref(i->object);

        if (i->memchunk.memblock)
            pa_memblock_unref(i->memchunk.memblock);

        if (i->free_cb)
            i->free_cb(i->userdata);

        if (pa_flist_push(PA_STATIC_FLIST_GET(asyncmsgq), i) < 0)
            pa_xfree(i);
    }

    pa_asyncq_free(a->asyncq, NULL);
    pa_mutex_free(a->mutex);
    pa_xfree(a);
}

pa_asyncmsgq* pa_asyncmsgq_ref(pa_asyncmsgq *q) {
    pa_assert(PA_REFCNT_VALUE(q) > 0);

    PA_REFCNT_INC(q);
    return q;
}

void pa_asyncmsgq_unref(pa_asyncmsgq* q) {
    pa_assert(PA_REFCNT_VALUE(q) > 0);

    if (PA_REFCNT_DEC(q) <= 0)
        asyncmsgq_free(q);
}

void pa_asyncmsgq_post(pa_asyncmsgq *a, pa_msgobject *object, int code, const void *userdata, int64_t offset, const pa_memchunk *chunk, pa_free_cb_t free_cb) {
    struct asyncmsgq_item *i;
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    if (!(i = pa_flist_pop(PA_STATIC_FLIST_GET(asyncmsgq))))
        i = pa_xnew(struct asyncmsgq_item, 1);

    i->code = code;
    i->object = object ? pa_msgobject_ref(object) : NULL;
    i->userdata = (void*) userdata;
    i->free_cb = free_cb;
    i->offset = offset;
    if (chunk) {
        pa_assert(chunk->memblock);
        i->memchunk = *chunk;
        pa_memblock_ref(i->memchunk.memblock);
    } else
        pa_memchunk_reset(&i->memchunk);
    i->semaphore = NULL;

    /* This mutex makes the queue multiple-writer safe. This lock is only used on the writing side */
    pa_mutex_lock(a->mutex);
    pa_asyncq_post(a->asyncq, i);
    pa_mutex_unlock(a->mutex);
}

int pa_asyncmsgq_send(pa_asyncmsgq *a, pa_msgobject *object, int code, const void *userdata, int64_t offset, const pa_memchunk *chunk) {
    struct asyncmsgq_item i;
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    i.code = code;
    i.object = object;
    i.userdata = (void*) userdata;
    i.free_cb = NULL;
    i.ret = -1;
    i.offset = offset;
    if (chunk) {
        pa_assert(chunk->memblock);
        i.memchunk = *chunk;
    } else
        pa_memchunk_reset(&i.memchunk);

    if (!(i.semaphore = pa_flist_pop(PA_STATIC_FLIST_GET(semaphores))))
        i.semaphore = pa_semaphore_new(0);

    /* This mutex makes the queue multiple-writer safe. This lock is only used on the writing side */
    pa_mutex_lock(a->mutex);
    pa_assert_se(pa_asyncq_push(a->asyncq, &i, true) == 0);
    pa_mutex_unlock(a->mutex);

    pa_semaphore_wait(i.semaphore);

    if (pa_flist_push(PA_STATIC_FLIST_GET(semaphores), i.semaphore) < 0)
        pa_semaphore_free(i.semaphore);

    return i.ret;
}

int pa_asyncmsgq_get(pa_asyncmsgq *a, pa_msgobject **object, int *code, void **userdata, int64_t *offset, pa_memchunk *chunk, bool wait_op) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);
    pa_assert(!a->current);

    if (!(a->current = pa_asyncq_pop(a->asyncq, wait_op))) {
/*         pa_log("failure"); */
        return -1;
    }

/*     pa_log("success"); */

    if (code)
        *code = a->current->code;
    if (userdata)
        *userdata = a->current->userdata;
    if (offset)
        *offset = a->current->offset;
    if (object) {
        if ((*object = a->current->object))
            pa_msgobject_assert_ref(*object);
    }
    if (chunk)
        *chunk = a->current->memchunk;

/*     pa_log_debug("Get q=%p object=%p (%s) code=%i data=%p chunk.length=%lu", */
/*                  (void*) a, */
/*                  (void*) a->current->object, */
/*                  a->current->object ? a->current->object->parent.type_name : NULL, */
/*                  a->current->code, */
/*                  (void*) a->current->userdata, */
/*                  (unsigned long) a->current->memchunk.length); */

    return 0;
}

void pa_asyncmsgq_done(pa_asyncmsgq *a, int ret) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);
    pa_assert(a);
    pa_assert(a->current);

    if (a->current->semaphore) {
        a->current->ret = ret;
        pa_semaphore_post(a->current->semaphore);
    } else {

        if (a->current->free_cb)
            a->current->free_cb(a->current->userdata);

        if (a->current->object)
            pa_msgobject_unref(a->current->object);

        if (a->current->memchunk.memblock)
            pa_memblock_unref(a->current->memchunk.memblock);

        if (pa_flist_push(PA_STATIC_FLIST_GET(asyncmsgq), a->current) < 0)
            pa_xfree(a->current);
    }

    a->current = NULL;
}

int pa_asyncmsgq_wait_for(pa_asyncmsgq *a, int code) {
    int c;
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    pa_asyncmsgq_ref(a);

    do {
        pa_msgobject *o;
        void *data;
        int64_t offset;
        pa_memchunk chunk;
        int ret;

        if (pa_asyncmsgq_get(a, &o, &c, &data, &offset, &chunk, true) < 0)
            return -1;

        ret = pa_asyncmsgq_dispatch(o, c, data, offset, &chunk);
        pa_asyncmsgq_done(a, ret);

    } while (c != code);

    pa_asyncmsgq_unref(a);

    return 0;
}

int pa_asyncmsgq_process_one(pa_asyncmsgq *a) {
    pa_msgobject *object;
    int code;
    void *data;
    pa_memchunk chunk;
    int64_t offset;
    int ret;

    pa_assert(PA_REFCNT_VALUE(a) > 0);

    if (pa_asyncmsgq_get(a, &object, &code, &data, &offset, &chunk, false) < 0)
        return 0;

    pa_asyncmsgq_ref(a);
    ret = pa_asyncmsgq_dispatch(object, code, data, offset, &chunk);
    pa_asyncmsgq_done(a, ret);
    pa_asyncmsgq_unref(a);

    return 1;
}

int pa_asyncmsgq_read_fd(pa_asyncmsgq *a) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    return pa_asyncq_read_fd(a->asyncq);
}

int pa_asyncmsgq_read_before_poll(pa_asyncmsgq *a) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    return pa_asyncq_read_before_poll(a->asyncq);
}

void pa_asyncmsgq_read_after_poll(pa_asyncmsgq *a) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    pa_asyncq_read_after_poll(a->asyncq);
}

int pa_asyncmsgq_write_fd(pa_asyncmsgq *a) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    return pa_asyncq_write_fd(a->asyncq);
}

void pa_asyncmsgq_write_before_poll(pa_asyncmsgq *a) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    pa_asyncq_write_before_poll(a->asyncq);
}

void pa_asyncmsgq_write_after_poll(pa_asyncmsgq *a) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    pa_asyncq_write_after_poll(a->asyncq);
}

int pa_asyncmsgq_dispatch(pa_msgobject *object, int code, void *userdata, int64_t offset, pa_memchunk *memchunk) {

    if (object)
        return object->process_msg(object, code, userdata, offset, pa_memchunk_isset(memchunk) ? memchunk : NULL);

    return 0;
}

void pa_asyncmsgq_flush(pa_asyncmsgq *a, bool run) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    for (;;) {
        pa_msgobject *object;
        int code;
        void *data;
        int64_t offset;
        pa_memchunk chunk;
        int ret;

        if (pa_asyncmsgq_get(a, &object, &code, &data, &offset, &chunk, false) < 0)
            return;

        if (!run) {
            pa_asyncmsgq_done(a, -1);
            continue;
        }

        pa_asyncmsgq_ref(a);
        ret = pa_asyncmsgq_dispatch(object, code, data, offset, &chunk);
        pa_asyncmsgq_done(a, ret);
        pa_asyncmsgq_unref(a);
    }
}

bool pa_asyncmsgq_dispatching(pa_asyncmsgq *a) {
    pa_assert(PA_REFCNT_VALUE(a) > 0);

    return !!a->current;
}
