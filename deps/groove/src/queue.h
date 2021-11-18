/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_QUEUE_H
#define GROOVE_QUEUE_H

struct GrooveQueue {
    void *context;
    // defaults to groove_queue_cleanup_default
    void (*cleanup)(struct GrooveQueue*, void *obj);
    void (*put)(struct GrooveQueue*, void *obj);
    void (*get)(struct GrooveQueue*, void *obj);
    int (*purge)(struct GrooveQueue*, void *obj);
};

struct GrooveQueue *groove_queue_create(void);

void groove_queue_flush(struct GrooveQueue *queue);

void groove_queue_destroy(struct GrooveQueue *queue);

void groove_queue_abort(struct GrooveQueue *queue);
void groove_queue_reset(struct GrooveQueue *queue);

int groove_queue_put(struct GrooveQueue *queue, void *obj);

// returns -1 if aborted, 1 if got event, 0 if no event ready
int groove_queue_get(struct GrooveQueue *queue, void **obj_ptr, int block);

int groove_queue_peek(struct GrooveQueue *queue, int block);

void groove_queue_purge(struct GrooveQueue *queue);

void groove_queue_cleanup_default(struct GrooveQueue *queue, void *obj);

#endif
