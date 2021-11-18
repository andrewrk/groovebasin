/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "queue.h"
#include "util.h"

#include <pthread.h>

struct ItemList {
    void *obj;
    struct ItemList *next;
};

struct GrooveQueuePrivate {
    struct GrooveQueue externals;
    struct ItemList *first;
    struct ItemList *last;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int abort_request;
};

struct GrooveQueue *groove_queue_create(void) {
    struct GrooveQueuePrivate *q = ALLOCATE(struct GrooveQueuePrivate, 1);
    if (!q)
        return NULL;

    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        DEALLOCATE(q);
        return NULL;
    }
    if (pthread_cond_init(&q->cond, NULL) != 0) {
        DEALLOCATE(q);
        pthread_mutex_destroy(&q->mutex);
        return NULL;
    }
    struct GrooveQueue *queue = &q->externals;
    queue->cleanup = groove_queue_cleanup_default;
    return queue;
}

void groove_queue_flush(struct GrooveQueue *queue) {
    struct GrooveQueuePrivate *q = (struct GrooveQueuePrivate *) queue;

    pthread_mutex_lock(&q->mutex);

    struct ItemList *el;
    struct ItemList *el1;
    for (el = q->first; el != NULL; el = el1) {
        el1 = el->next;
        if (queue->cleanup)
            queue->cleanup(queue, el->obj);
        DEALLOCATE(el);
    }
    q->first = NULL;
    q->last = NULL;

    pthread_mutex_unlock(&q->mutex);
}

void groove_queue_destroy(struct GrooveQueue *queue) {
    groove_queue_flush(queue);
    struct GrooveQueuePrivate *q = (struct GrooveQueuePrivate *) queue;
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
    DEALLOCATE(q);
}

void groove_queue_abort(struct GrooveQueue *queue) {
    struct GrooveQueuePrivate *q = (struct GrooveQueuePrivate *) queue;

    pthread_mutex_lock(&q->mutex);

    q->abort_request = 1;

    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

void groove_queue_reset(struct GrooveQueue *queue) {
    struct GrooveQueuePrivate *q = (struct GrooveQueuePrivate *) queue;

    pthread_mutex_lock(&q->mutex);

    q->abort_request = 0;

    pthread_mutex_unlock(&q->mutex);
}

int groove_queue_put(struct GrooveQueue *queue, void *obj) {
    struct ItemList * el1 = ALLOCATE(struct ItemList, 1);

    if (!el1)
        return GrooveErrorNoMem;

    el1->obj = obj;

    struct GrooveQueuePrivate *q = (struct GrooveQueuePrivate *) queue;
    pthread_mutex_lock(&q->mutex);

    if (!q->last)
        q->first = el1;
    else
        q->last->next = el1;
    q->last = el1;

    if (queue->put)
        queue->put(queue, obj);

    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);

    return 0;
}

int groove_queue_peek(struct GrooveQueue *queue, int block) {
    int ret;

    struct GrooveQueuePrivate *q = (struct GrooveQueuePrivate *) queue;
    pthread_mutex_lock(&q->mutex);

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        if (q->first) {
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            pthread_cond_wait(&q->cond, &q->mutex);
        }
    }

    pthread_mutex_unlock(&q->mutex);
    return ret;
}

int groove_queue_get(struct GrooveQueue *queue, void **obj_ptr, int block) {
    struct ItemList *ev1;
    int ret;

    struct GrooveQueuePrivate *q = (struct GrooveQueuePrivate *) queue;
    pthread_mutex_lock(&q->mutex);

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        ev1 = q->first;
        if (ev1) {
            q->first = ev1->next;
            if (!q->first)
                q->last = NULL;

            if (queue->get)
                queue->get(queue, ev1->obj);

            *obj_ptr = ev1->obj;
            DEALLOCATE(ev1);
            ret = 1;
            break;
        } else if(!block) {
            ret = 0;
            break;
        } else {
            pthread_cond_wait(&q->cond, &q->mutex);
        }
    }

    pthread_mutex_unlock(&q->mutex);
    return ret;
}

void groove_queue_purge(struct GrooveQueue *queue) {
    struct GrooveQueuePrivate *q = (struct GrooveQueuePrivate *) queue;

    pthread_mutex_lock(&q->mutex);
    struct ItemList *node = q->first;
    struct ItemList *prev = NULL;
    while (node) {
        if (queue->purge(queue, node->obj)) {
            if (prev) {
                prev->next = node->next;
                if (queue->cleanup)
                    queue->cleanup(queue, node->obj);
                DEALLOCATE(node);
                node = prev->next;
                if (!node)
                    q->last = prev;
            } else {
                struct ItemList *next = node->next;
                if (queue->cleanup)
                    queue->cleanup(queue, node->obj);
                DEALLOCATE(node);
                q->first = next;
                node = next;
                if (!node)
                    q->last = NULL;
            }
        } else {
            prev = node;
            node = node->next;
        }
    }
    pthread_mutex_unlock(&q->mutex);
}

void groove_queue_cleanup_default(struct GrooveQueue *queue, void *obj) {
    DEALLOCATE(obj);
}

