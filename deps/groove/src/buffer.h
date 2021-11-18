/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_BUFFER_H
#define GROOVE_BUFFER_H

#include "groove_internal.h"

#include <pthread.h>

#include <libavutil/frame.h>

struct GrooveBufferPrivate {
    struct GrooveBuffer externals;
    AVFrame *frame;
    int is_packet;
    int ref_count;

    pthread_mutex_t mutex;
    // used for when is_packet is true
    // GrooveBuffer::data[0] will point to this
    uint8_t *data;
};

#endif
