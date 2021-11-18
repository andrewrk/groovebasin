/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "groove_internal.h"
#include "groove/waveform.h"
#include "util.h"
#include "queue.h"

#include <pthread.h>

static const int sample_rate = 44100;

struct GrooveWaveformPrivate {
    struct GrooveWaveform externals;

    struct Groove *groove;
    struct GrooveSink *sink;
    struct GrooveQueue *info_queue;
    int info_queue_bytes;
    pthread_t thread_id;
    struct GrooveWaveformInfo *cur_info;
    int cur_data_index;

    // calculated, but we don't have the correct answer until too late
    long actual_track_frame_count;
    // from the GrooveFile of the GroovePlaylistItem
    double estimated_track_duration;
    // from the GrooveFile of the GroovePlaylistItem
    long estimated_track_frame_count;
    int track_frames_per_pixel;
    int frames_until_emit;
    int emit_count;
    float max_sample_value;

    pthread_mutex_t info_head_mutex;
    bool info_head_mutex_inited;
    // current playlist item pointer
    struct GroovePlaylistItem *info_head;
    double info_pos;
    // analyze_thread waits on this when the info queue is full
    pthread_cond_t drain_cond;
    bool drain_cond_inited;

    // set temporarily
    struct GroovePlaylistItem *purge_item;

    int abort_request;
};

static int bytes_per_format(enum GrooveWaveformFormat format) {
    switch (format) {
        case GrooveWaveformFormatU8:
            return 1;
    }
    groove_panic("invalid GrooveWaveformFormat");
}

static struct GrooveWaveformInfo *create_info(struct GrooveWaveformPrivate *w, struct GroovePlaylistItem *item) {
    struct GrooveWaveform *waveform = &w->externals;
    int payload_bytes = item ? (waveform->width_in_frames * bytes_per_format(waveform->format)) : 0;
    struct GrooveWaveformInfo *info = (struct GrooveWaveformInfo *)ALLOCATE(char,
            sizeof(struct GrooveWaveformInfo) + payload_bytes);
    if (!info)
        groove_panic("memory allocation failure");
    info->ref_count = 1;
    info->item = item;
    info->data_size = payload_bytes;
    return info;
}

static int info_size(struct GrooveWaveformInfo *info) {
    return sizeof(struct GrooveWaveformInfo) + info->data_size;
}

static void emit_track_info(struct GrooveWaveformPrivate *w) {
    struct GrooveWaveform *waveform = &w->externals;

    if (!w->cur_info)
        return;

    if (w->emit_count < waveform->width_in_frames) {
        // emit the last sample
        uint8_t *ptr = (uint8_t *)&w->cur_info->data[w->cur_data_index];
        *ptr = w->max_sample_value * UINT8_MAX;
    }

    w->cur_info->sample_rate = sample_rate;
    w->cur_info->expected_frame_count = (long)(w->estimated_track_duration * sample_rate + 0.5);
    w->cur_info->actual_frame_count = w->actual_track_frame_count;
    int err;
    if ((err = groove_queue_put(w->info_queue, w->cur_info)))
        groove_panic("unable to put in queue: %s", groove_strerror(err));

    w->cur_info = NULL;
}

static void *waveform_thread(void *arg) {
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *)arg;
    struct GrooveWaveform *waveform = &w->externals;

    struct GrooveBuffer *buffer;

    pthread_mutex_lock(&w->info_head_mutex);
    while (!w->abort_request) {
        if (w->info_queue_bytes >= waveform->info_queue_size_bytes) {
            pthread_cond_wait(&w->drain_cond, &w->info_head_mutex);
            continue;
        }

        // we definitely want to unlock the mutex while we wait for the
        // next buffer. Otherwise there will be a deadlock when sink_flush or
        // sink_purge is called.
        pthread_mutex_unlock(&w->info_head_mutex);

        int result = groove_sink_buffer_get(w->sink, &buffer, 1);

        pthread_mutex_lock(&w->info_head_mutex);

        if (result == GROOVE_BUFFER_END) {
            emit_track_info(w);

            int err;
            if ((err = groove_queue_put(w->info_queue, create_info(w, NULL))))
                groove_panic("unable to put in queue: %s", groove_strerror(err));

            w->info_head = NULL;
            w->info_pos = -1.0;
            continue;
        }

        if (result != GROOVE_BUFFER_YES) {
            break;
        }

        if (buffer->item != w->info_head) {
            emit_track_info(w);

            if (buffer->item) {
                // start a track
                struct GrooveFile *file = buffer->item->file;
                w->estimated_track_duration = (file->override_duration != 0.0) ?
                    file->override_duration : groove_file_duration(file);
                if (w->estimated_track_duration <= 0.0) {
                    w->estimated_track_duration = 0.0;
                }
                w->estimated_track_frame_count = sample_rate * w->estimated_track_duration;
                w->track_frames_per_pixel = w->estimated_track_frame_count / waveform->width_in_frames;
                w->track_frames_per_pixel = groove_max_int(w->track_frames_per_pixel, 1);
                w->frames_until_emit = w->track_frames_per_pixel;
                w->emit_count = 0;
                w->max_sample_value = 0.0f;

                w->actual_track_frame_count = 0;
                w->info_head = buffer->item;
                w->info_pos = buffer->pos;

                w->cur_info = create_info(w, buffer->item);
                w->cur_data_index = 0;
            }
        }

        w->actual_track_frame_count += buffer->frame_count;

        for (int i = 0; i < buffer->frame_count && w->emit_count < waveform->width_in_frames;
                i += 1, w->frames_until_emit -= 1)
        {
            if (w->frames_until_emit == 0) {
                w->emit_count += 1;
                uint8_t *ptr = (uint8_t *)&w->cur_info->data[w->cur_data_index];
                *ptr = w->max_sample_value * UINT8_MAX;
                w->cur_data_index += 1;

                w->max_sample_value = 0.0f;
                w->frames_until_emit = w->track_frames_per_pixel;
            }
            float *data = (float *) buffer->data[0];
            float *left = &data[i];
            float *right = &data[i + 1];
            float abs_left = fabsf(*left);
            float abs_right = fabsf(*right);
            w->max_sample_value = groove_max_float(w->max_sample_value, groove_max_float(abs_left, abs_right));
        }

        groove_buffer_unref(buffer);
    }
    pthread_mutex_unlock(&w->info_head_mutex);

    return NULL;
}

static void info_queue_cleanup(struct GrooveQueue* queue, void *obj) {
    struct GrooveWaveformInfo *info = (struct GrooveWaveformInfo *)obj;
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *)queue->context;
    w->info_queue_bytes -= info_size(info);
    groove_waveform_info_unref(info);
}

static void info_queue_put(struct GrooveQueue *queue, void *obj) {
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *)queue->context;
    struct GrooveWaveformInfo *info = (struct GrooveWaveformInfo *)obj;
    w->info_queue_bytes += info_size(info);
}

static void info_queue_get(struct GrooveQueue *queue, void *obj) {
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *)queue->context;
    struct GrooveWaveform *waveform = &w->externals;
    struct GrooveWaveformInfo *info = (struct GrooveWaveformInfo *)obj;

    w->info_queue_bytes -= info_size(info);

    if (w->info_queue_bytes < waveform->info_queue_size_bytes)
        pthread_cond_signal(&w->drain_cond);
}

static int info_queue_purge(struct GrooveQueue* queue, void *obj) {
    struct GrooveWaveformInfo *info = (struct GrooveWaveformInfo *)obj;
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *)queue->context;

    return info->item == w->purge_item;
}

static void sink_purge(struct GrooveSink *sink, struct GroovePlaylistItem *item) {
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *)sink->userdata;

    pthread_mutex_lock(&w->info_head_mutex);
    w->purge_item = item;
    groove_queue_purge(w->info_queue);
    w->purge_item = NULL;

    if (w->info_head == item) {
        w->info_head = NULL;
        w->info_pos = -1.0;
    }
    pthread_cond_signal(&w->drain_cond);
    pthread_mutex_unlock(&w->info_head_mutex);
}

static void sink_flush(struct GrooveSink *sink) {
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *)sink->userdata;

    pthread_mutex_lock(&w->info_head_mutex);
    groove_queue_flush(w->info_queue);
    groove_waveform_info_unref(w->cur_info);
    w->cur_data_index = 0;
    w->cur_info = NULL;
    w->actual_track_frame_count = 0.0;
    w->info_head = NULL;
    w->info_pos = -1.0;

    pthread_cond_signal(&w->drain_cond);
    pthread_mutex_unlock(&w->info_head_mutex);
}

struct GrooveWaveform *groove_waveform_create(struct Groove *groove) {
    struct GrooveWaveformPrivate *w = ALLOCATE(struct GrooveWaveformPrivate, 1);
    if (!w)
        return NULL;

    w->groove = groove;

    struct GrooveWaveform *waveform = &w->externals;

    if (pthread_mutex_init(&w->info_head_mutex, NULL) != 0) {
        groove_waveform_destroy(waveform);
        return NULL;
    }

    w->info_head_mutex_inited = true;

    if (pthread_cond_init(&w->drain_cond, NULL) != 0) {
        groove_waveform_destroy(waveform);
        return NULL;
    }
    w->drain_cond_inited = 1;

    w->info_queue = groove_queue_create();
    if (!w->info_queue) {
        groove_waveform_destroy(waveform);
        return NULL;
    }
    w->info_queue->context = w;
    w->info_queue->cleanup = info_queue_cleanup;
    w->info_queue->put = info_queue_put;
    w->info_queue->get = info_queue_get;
    w->info_queue->purge = info_queue_purge;

    w->sink = groove_sink_create(groove);
    if (!w->sink) {
        groove_waveform_destroy(waveform);
        return NULL;
    }

    struct GrooveAudioFormat audio_format;
    audio_format.sample_rate = sample_rate;
    audio_format.layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
    audio_format.format = SoundIoFormatFloat32NE;
    audio_format.is_planar = false;

    groove_sink_set_only_format(w->sink, &audio_format);
    w->sink->userdata = waveform;
    w->sink->purge = sink_purge;
    w->sink->flush = sink_flush;

    // set some defaults
    waveform->width_in_frames = 1920;
    waveform->format = GrooveWaveformFormatU8;
    waveform->info_queue_size_bytes = INT_MAX;
    waveform->sink_buffer_size_bytes = w->sink->buffer_size_bytes;

    return waveform;
}

void groove_waveform_destroy(struct GrooveWaveform *waveform) {
    if (!waveform)
        return;

    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *) waveform;

    if (w->sink)
        groove_sink_destroy(w->sink);

    if (w->info_queue)
        groove_queue_destroy(w->info_queue);

    if (w->info_head_mutex_inited)
        pthread_mutex_destroy(&w->info_head_mutex);

    if (w->drain_cond_inited)
        pthread_cond_destroy(&w->drain_cond);

    DEALLOCATE(w);
}

int groove_waveform_attach(struct GrooveWaveform *waveform,
        struct GroovePlaylist *playlist)
{
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *) waveform;

    if (!waveform->width_in_frames)
        return GrooveErrorInvalid;

    waveform->playlist = playlist;
    groove_queue_reset(w->info_queue);

    int err;
    if ((err = groove_sink_attach(w->sink, playlist))) {
        groove_waveform_detach(waveform);
        return err;
    }

    if (pthread_create(&w->thread_id, NULL, waveform_thread, waveform)) {
        groove_waveform_detach(waveform);
        return GrooveErrorSystemResources;
    }

    return 0;
}

int groove_waveform_detach(struct GrooveWaveform *waveform) {
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *) waveform;

    pthread_mutex_lock(&w->info_head_mutex);
    w->abort_request = 1;
    pthread_cond_signal(&w->drain_cond);
    pthread_mutex_unlock(&w->info_head_mutex);

    int err = groove_sink_detach(w->sink);
    assert(!err);
    groove_queue_flush(w->info_queue);
    groove_queue_abort(w->info_queue);
    pthread_join(w->thread_id, NULL);

    waveform->playlist = NULL;

    w->abort_request = 0;
    w->info_head = NULL;
    w->info_pos = 0;
    w->actual_track_frame_count = 0.0;

    return 0;
}

int groove_waveform_info_get(struct GrooveWaveform *waveform,
        struct GrooveWaveformInfo **info, int block)
{
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *) waveform;

    if (groove_queue_get(w->info_queue, (void**)info, block) == 1) {
        return 1;
    }

    return 0;
}

int groove_waveform_info_peek(struct GrooveWaveform *waveform, int block) {
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *) waveform;
    return groove_queue_peek(w->info_queue, block);
}

void groove_waveform_position(struct GrooveWaveform *waveform,
        struct GroovePlaylistItem **item, double *seconds)
{
    struct GrooveWaveformPrivate *w = (struct GrooveWaveformPrivate *) waveform;

    pthread_mutex_lock(&w->info_head_mutex);

    if (item)
        *item = w->info_head;

    if (seconds)
        *seconds = w->info_pos;

    pthread_mutex_unlock(&w->info_head_mutex);
}

void groove_waveform_info_ref(struct GrooveWaveformInfo *info) {
    info->ref_count += 1;
}

void groove_waveform_info_unref(struct GrooveWaveformInfo *info) {
    info->ref_count -= 1;
    assert(info->ref_count >= 0);
    if (info->ref_count == 0) {
        DEALLOCATE(info);
    }
}
