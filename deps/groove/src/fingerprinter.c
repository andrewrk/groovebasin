/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "groove_internal.h"
#include "groove/fingerprinter.h"
#include "queue.h"
#include "util.h"
#include "atomics.h"

#include <chromaprint/src/chromaprint.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct GrooveFingerprinterPrivate {
    struct GrooveFingerprinter externals;

    struct Groove *groove;
    int state_history_count;

    // index into all_track_states
    struct GrooveSink *sink;
    struct GrooveQueue *info_queue;
    pthread_t thread_id;

    // info_head_mutex applies to variables inside this block.
    pthread_mutex_t info_head_mutex;
    char info_head_mutex_inited;
    // current playlist item pointer
    struct GroovePlaylistItem *info_head;
    double info_pos;
    // analyze_thread waits on this when the info queue is full
    pthread_cond_t drain_cond;
    char drain_cond_inited;
    // how many items are in the queue
    int info_queue_count;
    double track_duration;
    double album_duration;

    ChromaprintContext *chroma_ctx;

    // set temporarily
    struct GroovePlaylistItem *purge_item;

    struct GrooveAtomicBool abort_request;
};

static int emit_track_info(struct GrooveFingerprinterPrivate *p) {
    struct GrooveFingerprinterInfo *info = ALLOCATE(struct GrooveFingerprinterInfo, 1);
    if (!info) {
        return GrooveErrorNoMem;
    }
    info->item = p->info_head;
    info->duration = p->track_duration;

    if (!chromaprint_finish(p->chroma_ctx)) {
        av_log(NULL, AV_LOG_ERROR, "unable to finish chromaprint\n");
        return GrooveErrorNoMem;
    }
    if (!chromaprint_get_raw_fingerprint(p->chroma_ctx,
                (void**)&info->fingerprint, &info->fingerprint_size))
    {
        av_log(NULL, AV_LOG_ERROR, "unable to get fingerprint\n");
        return GrooveErrorNoMem;
    }

    groove_queue_put(p->info_queue, info);

    return 0;
}

static void *print_thread(void *arg) {
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *)arg;
    struct GrooveFingerprinter *printer = &p->externals;

    struct GrooveBuffer *buffer;
    while (!GROOVE_ATOMIC_LOAD(p->abort_request)) {
        pthread_mutex_lock(&p->info_head_mutex);

        if (p->info_queue_count >= printer->info_queue_size) {
            pthread_cond_wait(&p->drain_cond, &p->info_head_mutex);
            pthread_mutex_unlock(&p->info_head_mutex);
            continue;
        }

        // we definitely want to unlock the mutex while we wait for the
        // next buffer. Otherwise there will be a deadlock when sink_flush or
        // sink_purge is called.
        pthread_mutex_unlock(&p->info_head_mutex);

        int result = groove_sink_buffer_get(p->sink, &buffer, 1);

        pthread_mutex_lock(&p->info_head_mutex);

        if (result == GROOVE_BUFFER_END) {
            // last file info
            emit_track_info(p);

            // send album info
            struct GrooveFingerprinterInfo *info = ALLOCATE(struct GrooveFingerprinterInfo, 1);
            if (info) {
                info->duration = p->album_duration;
                groove_queue_put(p->info_queue, info);
            } else {
                av_log(NULL, AV_LOG_ERROR, "unable to allocate album fingerprint info\n");
            }

            p->album_duration = 0.0;

            p->info_head = NULL;
            p->info_pos = -1.0;

            pthread_mutex_unlock(&p->info_head_mutex);
            continue;
        }

        if (result != GROOVE_BUFFER_YES) {
            pthread_mutex_unlock(&p->info_head_mutex);
            break;
        }

        if (buffer->item != p->info_head) {
            if (p->info_head) {
                emit_track_info(p);
            }
            if (!chromaprint_start(p->chroma_ctx, 44100, 2)) {
                av_log(NULL, AV_LOG_ERROR, "unable to start fingerprint\n");
            }
            p->track_duration = 0.0;
            p->info_head = buffer->item;
            p->info_pos = buffer->pos;
        }

        double buffer_duration = buffer->frame_count / (double)buffer->format.sample_rate;
        p->track_duration += buffer_duration;
        p->album_duration += buffer_duration;
        if (!chromaprint_feed(p->chroma_ctx, buffer->data[0], buffer->frame_count * 2)) {
            av_log(NULL, AV_LOG_ERROR, "unable to feed fingerprint\n");
        }

        pthread_mutex_unlock(&p->info_head_mutex);
        groove_buffer_unref(buffer);
    }

    return NULL;
}

static void info_queue_cleanup(struct GrooveQueue* queue, void *obj) {
    struct GrooveFingerprinterInfo *info = (struct GrooveFingerprinterInfo *)obj;
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *)queue->context;
    p->info_queue_count -= 1;
    DEALLOCATE(info);
}

static void info_queue_put(struct GrooveQueue *queue, void *obj) {
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *)queue->context;
    p->info_queue_count += 1;
}

static void info_queue_get(struct GrooveQueue *queue, void *obj) {
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *)queue->context;
    struct GrooveFingerprinter *printer = &p->externals;

    p->info_queue_count -= 1;

    if (p->info_queue_count < printer->info_queue_size)
        pthread_cond_signal(&p->drain_cond);
}

static int info_queue_purge(struct GrooveQueue* queue, void *obj) {
    struct GrooveFingerprinterInfo *info = (struct GrooveFingerprinterInfo *)obj;
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *)queue->context;

    return info->item == p->purge_item;
}

static void sink_purge(struct GrooveSink *sink, struct GroovePlaylistItem *item) {
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *)sink->userdata;

    pthread_mutex_lock(&p->info_head_mutex);
    p->purge_item = item;
    groove_queue_purge(p->info_queue);
    p->purge_item = NULL;

    if (p->info_head == item) {
        p->info_head = NULL;
        p->info_pos = -1.0;
    }
    pthread_cond_signal(&p->drain_cond);
    pthread_mutex_unlock(&p->info_head_mutex);
}

static void sink_flush(struct GrooveSink *sink) {
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *)sink->userdata;

    pthread_mutex_lock(&p->info_head_mutex);
    groove_queue_flush(p->info_queue);
    p->track_duration = 0.0;
    p->info_head = NULL;
    p->info_pos = -1.0;

    pthread_cond_signal(&p->drain_cond);
    pthread_mutex_unlock(&p->info_head_mutex);
}

struct GrooveFingerprinter *groove_fingerprinter_create(struct Groove *groove) {
    struct GrooveFingerprinterPrivate *p = ALLOCATE(struct GrooveFingerprinterPrivate, 1);
    if (!p) {
        av_log(NULL, AV_LOG_ERROR, "unable to allocate fingerprinter\n");
        return NULL;
    }
    GROOVE_ATOMIC_STORE(p->abort_request, false);
    p->groove = groove;

    struct GrooveFingerprinter *printer = &p->externals;

    if (pthread_mutex_init(&p->info_head_mutex, NULL) != 0) {
        groove_fingerprinter_destroy(printer);
        av_log(NULL, AV_LOG_ERROR, "unable to create mutex\n");
        return NULL;
    }
    p->info_head_mutex_inited = 1;

    if (pthread_cond_init(&p->drain_cond, NULL) != 0) {
        groove_fingerprinter_destroy(printer);
        av_log(NULL, AV_LOG_ERROR, "unable to create mutex condition\n");
        return NULL;
    }
    p->drain_cond_inited = 1;

    p->info_queue = groove_queue_create();
    if (!p->info_queue) {
        groove_fingerprinter_destroy(printer);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate queue\n");
        return NULL;
    }
    p->info_queue->context = printer;
    p->info_queue->cleanup = info_queue_cleanup;
    p->info_queue->put = info_queue_put;
    p->info_queue->get = info_queue_get;
    p->info_queue->purge = info_queue_purge;

    p->sink = groove_sink_create(groove);
    if (!p->sink) {
        groove_fingerprinter_destroy(printer);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate sink\n");
        return NULL;
    }

    struct GrooveAudioFormat audio_format;
    audio_format.sample_rate = 44100;
    audio_format.layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
    audio_format.format = SoundIoFormatS16NE;
    audio_format.is_planar = false;

    groove_sink_set_only_format(p->sink, &audio_format);
    p->sink->userdata = printer;
    p->sink->purge = sink_purge;
    p->sink->flush = sink_flush;

    // set some defaults
    printer->info_queue_size = INT_MAX;
    printer->sink_buffer_size_bytes = p->sink->buffer_size_bytes;

    return printer;
}

void groove_fingerprinter_destroy(struct GrooveFingerprinter *printer) {
    if (!printer)
        return;

    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *) printer;

    if (p->sink)
        groove_sink_destroy(p->sink);

    if (p->info_queue)
        groove_queue_destroy(p->info_queue);

    if (p->info_head_mutex_inited)
        pthread_mutex_destroy(&p->info_head_mutex);

    if (p->drain_cond_inited)
        pthread_cond_destroy(&p->drain_cond);

    DEALLOCATE(p);
}

int groove_fingerprinter_attach(struct GrooveFingerprinter *printer,
        struct GroovePlaylist *playlist)
{
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *) printer;

    printer->playlist = playlist;
    groove_queue_reset(p->info_queue);

    p->chroma_ctx = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);
    if (!p->chroma_ctx) {
        groove_fingerprinter_detach(printer);
        return GrooveErrorNoMem;
    }

    int err;
    if ((err = groove_sink_attach(p->sink, playlist))) {
        groove_fingerprinter_detach(printer);
        return err;
    }

    if (pthread_create(&p->thread_id, NULL, print_thread, printer)) {
        groove_fingerprinter_detach(printer);
        return GrooveErrorSystemResources;
    }

    return 0;
}

int groove_fingerprinter_detach(struct GrooveFingerprinter *printer) {
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *) printer;

    GROOVE_ATOMIC_STORE(p->abort_request, true);
    groove_sink_detach(p->sink);
    groove_queue_flush(p->info_queue);
    groove_queue_abort(p->info_queue);
    pthread_cond_signal(&p->drain_cond);
    pthread_join(p->thread_id, NULL);

    printer->playlist = NULL;

    if (p->chroma_ctx) {
        chromaprint_free(p->chroma_ctx);
        p->chroma_ctx = NULL;
    }

    GROOVE_ATOMIC_STORE(p->abort_request, false);
    p->info_head = NULL;
    p->info_pos = 0;
    p->track_duration = 0.0;

    return 0;
}

int groove_fingerprinter_info_get(struct GrooveFingerprinter *printer,
        struct GrooveFingerprinterInfo *info, int block)
{
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *) printer;

    struct GrooveFingerprinterInfo *info_ptr;
    if (groove_queue_get(p->info_queue, (void**)&info_ptr, block) == 1) {
        *info = *info_ptr;
        DEALLOCATE(info_ptr);
        return 1;
    }

    return 0;
}

int groove_fingerprinter_info_peek(struct GrooveFingerprinter *printer,
        int block)
{
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *) printer;
    return groove_queue_peek(p->info_queue, block);
}

void groove_fingerprinter_position(struct GrooveFingerprinter *printer,
        struct GroovePlaylistItem **item, double *seconds)
{
    struct GrooveFingerprinterPrivate *p = (struct GrooveFingerprinterPrivate *) printer;

    pthread_mutex_lock(&p->info_head_mutex);

    if (item)
        *item = p->info_head;

    if (seconds)
        *seconds = p->info_pos;

    pthread_mutex_unlock(&p->info_head_mutex);
}

void groove_fingerprinter_free_info(struct GrooveFingerprinterInfo *info) {
    if (!info->fingerprint) return;
    chromaprint_dealloc((void*)info->fingerprint);
    info->fingerprint = NULL;
}

int groove_fingerprinter_encode(int32_t *fp, int size, char **encoded_fp) {
    int encoded_size;
    int err = chromaprint_encode_fingerprint(fp, size,
            CHROMAPRINT_ALGORITHM_DEFAULT, (void **)encoded_fp, &encoded_size, 1);
    return err == 1 ? 0 : -1;
}

int groove_fingerprinter_decode(char *encoded_fp, int32_t **fp, int *size) {
    int algorithm;
    int encoded_size = strlen(encoded_fp);
    int err = chromaprint_decode_fingerprint(encoded_fp, encoded_size, (void**)fp, size,
            &algorithm, 1);
    return err == 1 ? 0 : -1;
}

void groove_fingerprinter_dealloc(void *ptr) {
    if (!ptr) return;
    chromaprint_dealloc(ptr);
}
