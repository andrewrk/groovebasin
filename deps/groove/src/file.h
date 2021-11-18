/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_FILE_H
#define GROOVE_FILE_H

#include "groove_internal.h"
#include "atomics.h"

#include <pthread.h>

#include <libavformat/avformat.h>

struct GrooveFilePrivate {
    struct GrooveFile externals;
    struct Groove *groove;
    int audio_stream_index;
    struct GrooveAtomicBool abort_request; // true when we're closing the file
    AVFormatContext *ic;
    AVCodec *decoder;
    AVStream *audio_st;
    unsigned char *avio_buf;
    AVIOContext *avio;
    struct GrooveCustomIo *custom_io;

    // this mutex protects the fields in this block
    pthread_mutex_t seek_mutex;
    int64_t seek_pos; // -1 if no seek request
    int seek_flush; // whether the seek request wants us to flush the buffer
    bool ever_seeked;

    int eof;
    double audio_clock; // position of the decode head
    AVPacket audio_pkt;

    // state while saving
    AVFormatContext *oc;
    int tempfile_exists;

    int paused;
    struct GrooveCustomIo prealloc_custom_io;
    FILE *stdfile;
};

#endif
