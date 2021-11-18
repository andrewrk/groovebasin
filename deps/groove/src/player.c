/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "groove_internal.h"
#include "groove/player.h"
#include "queue.h"
#include "util.h"
#include "atomics.h"
#include "os.h"

#include <soundio/soundio.h>
#include <assert.h>

struct GroovePlayerPrivate {
    struct GroovePlayer externals;

    struct Groove *groove;
    struct GrooveBuffer *audio_buf;
    size_t audio_buf_size; // in frames
    size_t audio_buf_index; // in frames

    int device_buffer_frames;
    int silence_frames_left;

    // this mutex applies to the variables in this block
    struct GrooveOsMutex *play_head_mutex;
    // pointer to current item where the buffered audio is reaching the device
    struct GroovePlaylistItem *play_head;
    // number of seconds into the play_head song where the buffered audio
    // is reaching the device
    double play_pos;
    // adjustment which takes into account hardware latency and sound card buffer
    double play_pos_adjustment;

    bool prebuffering;
    bool is_paused;
    bool is_started;

    struct SoundIoOutStream *outstream;
    struct GrooveSink *sink;

    struct GrooveQueue *eventq;

    // watchdog thread for opening and closing audio device
    bool abort_request;
    struct GrooveOsThread *helper_thread;
    struct GrooveOsCond *helper_thread_cond;
    bool request_device_close;
    bool request_device_open;
    struct GrooveAudioFormat device_format;
};

static enum SoundIoFormat prioritized_formats[] = {
    SoundIoFormatFloat32NE,
    SoundIoFormatS32NE,
    SoundIoFormatS16NE,
    SoundIoFormatFloat64NE,
    SoundIoFormatU8,
};

static enum SoundIoChannelLayoutId prioritized_layouts[] = {
    SoundIoChannelLayoutIdOctagonal,
    SoundIoChannelLayoutId7Point1WideBack,
    SoundIoChannelLayoutId7Point1Wide,
    SoundIoChannelLayoutId7Point1,
    SoundIoChannelLayoutId7Point0Front,
    SoundIoChannelLayoutId7Point0,
    SoundIoChannelLayoutId6Point1Front,
    SoundIoChannelLayoutId6Point1Back,
    SoundIoChannelLayoutId6Point1,
    SoundIoChannelLayoutIdHexagonal,
    SoundIoChannelLayoutId6Point0Front,
    SoundIoChannelLayoutId6Point0Side,
    SoundIoChannelLayoutId5Point1Back,
    SoundIoChannelLayoutId5Point0Back,
    SoundIoChannelLayoutId5Point1,
    SoundIoChannelLayoutId5Point0Side,
    SoundIoChannelLayoutId4Point1,
    SoundIoChannelLayoutIdQuad,
    SoundIoChannelLayoutId4Point0,
    SoundIoChannelLayoutId3Point1,
    SoundIoChannelLayoutId2Point1,
    SoundIoChannelLayoutIdStereo,
    SoundIoChannelLayoutIdMono,
};

static void emit_event(struct GrooveQueue *queue, enum GroovePlayerEventType type) {
    union GroovePlayerEvent *evt = ALLOCATE_NONZERO(union GroovePlayerEvent, 1);
    if (!evt) {
        av_log(NULL, AV_LOG_ERROR, "unable to create event: out of memory\n");
        return;
    }
    evt->type = type;
    if (groove_queue_put(queue, evt) < 0)
        av_log(NULL, AV_LOG_ERROR, "unable to put event on queue: out of memory\n");
}

static void close_audio_device(struct GroovePlayerPrivate *p) {
    soundio_outstream_destroy(p->outstream);
    p->outstream = NULL;
}

static void error_callback(struct SoundIoOutStream *outstream, int err) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *)outstream->userdata;
    av_log(NULL, AV_LOG_ERROR, "stream error: %s\n", soundio_strerror(err));
    emit_event(p->eventq, GROOVE_EVENT_STREAM_ERROR);
}

static void set_pause_state(struct GroovePlayerPrivate *p, bool new_state) {
    p->is_paused = new_state;
    if (p->is_started)
        soundio_outstream_pause(p->outstream, (p->prebuffering || new_state));
}

static void underflow_callback(struct SoundIoOutStream *outstream) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *)outstream->userdata;
    p->prebuffering = true;
    emit_event(p->eventq, GROOVE_EVENT_BUFFERUNDERRUN);
    groove_os_cond_signal(p->helper_thread_cond, p->play_head_mutex);
}

static bool audio_formats_equal_ignore_planar(
        const struct GrooveAudioFormat *a, const struct GrooveAudioFormat *b)
{
    return (a->sample_rate == b->sample_rate &&
            soundio_channel_layout_equal(&a->layout, &b->layout) &&
            a->format == b->format);
}

static void audio_callback(struct SoundIoOutStream *outstream,
        int frame_count_min, int frame_count_max)
{
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *)outstream->userdata;
    const struct SoundIoChannelLayout *layout = &outstream->layout;
    struct SoundIoChannelArea *areas;
    int err;

    int channel_count = layout->channel_count;
    int frames_left = frame_count_max;

    groove_os_mutex_lock(p->play_head_mutex);

    bool silence = p->prebuffering || p->request_device_close || p->is_paused;
    while (frames_left) {
        int frame_count = frames_left;

        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            error_callback(outstream, err);
            goto unlock_and_return;
        }

        if (!frame_count)
            break;

        while (frame_count > 0) {
            if (!silence && p->audio_buf_index >= p->audio_buf_size) {
                groove_buffer_unref(p->audio_buf);
                p->audio_buf_index = 0;
                p->audio_buf_size = 0;

                int ret = groove_sink_buffer_get(p->sink, &p->audio_buf, 0);
                if (ret == GROOVE_BUFFER_END) {
                    emit_event(p->eventq, GROOVE_EVENT_END_OF_PLAYLIST);
                    emit_event(p->eventq, GROOVE_EVENT_NOWPLAYING);
                    p->play_head = NULL;
                    p->play_pos = -1.0;
                    p->request_device_close = true;
                    silence = true;
                    p->silence_frames_left = p->device_buffer_frames;
                } else if (ret == GROOVE_BUFFER_YES) {
                    if (p->play_head != p->audio_buf->item)
                        emit_event(p->eventq, GROOVE_EVENT_NOWPLAYING);

                    p->play_head = p->audio_buf->item;
                    p->play_pos = p->audio_buf->pos;
                    p->audio_buf_size = p->audio_buf->frame_count;

                    if (!audio_formats_equal_ignore_planar(&p->audio_buf->format, &p->device_format)) {
                        p->request_device_close = true;
                        p->request_device_open = true;
                        silence = true;
                        p->silence_frames_left = p->device_buffer_frames;
                    }
                } else {
                    error_callback(outstream, SoundIoErrorUnderflow);
                    goto unlock_and_return;
                }
            }

            if (silence) {
                for (int frame = 0; frame < frame_count; frame += 1) {
                    for (int ch = 0; ch < channel_count; ch += 1) {
                        memset(areas[ch].ptr, 0, outstream->bytes_per_sample);
                        areas[ch].ptr += areas[ch].step;
                    }
                }
                frames_left -= frame_count;
                if (p->silence_frames_left > 0) {
                    p->silence_frames_left -= frame_count;
                    if (p->silence_frames_left <= 0) {
                        groove_os_cond_signal(p->helper_thread_cond, p->play_head_mutex);
                    }
                }
                frame_count = 0;
            } else {
                int audio_buf_frames_left = p->audio_buf_size - p->audio_buf_index;
                int write_frame_count = groove_min_int(frame_count, audio_buf_frames_left);
                size_t end_frame = p->audio_buf_index + write_frame_count;

                if (p->audio_buf->format.is_planar) {
                    for (; p->audio_buf_index < end_frame; p->audio_buf_index += 1) {
                        for (int ch = 0; ch < channel_count; ch += 1) {
                            uint8_t *source = &p->audio_buf->data[ch][p->audio_buf_index * outstream->bytes_per_sample];
                            memcpy(areas[ch].ptr, source, outstream->bytes_per_sample);
                            areas[ch].ptr += areas[ch].step;
                        }
                    }
                } else {
                    uint8_t *source = p->audio_buf->data[0] + p->audio_buf_index * outstream->bytes_per_frame;
                    for (; p->audio_buf_index < end_frame; p->audio_buf_index += 1) {
                        for (int ch = 0; ch < channel_count; ch += 1) {
                            memcpy(areas[ch].ptr, source, outstream->bytes_per_sample);
                            areas[ch].ptr += areas[ch].step;
                            source += outstream->bytes_per_sample;
                        }
                    }
                }

                frame_count -= write_frame_count;
                frames_left -= write_frame_count;
            }
        }

        if ((err = soundio_outstream_end_write(outstream))) {
            if (err == SoundIoErrorUnderflow) {
                underflow_callback(outstream);
                goto unlock_and_return;
            } else {
                error_callback(outstream, err);
                goto unlock_and_return;
            }
        }
    }

    soundio_outstream_get_latency(outstream, &p->play_pos_adjustment);

unlock_and_return:
    groove_os_mutex_unlock(p->play_head_mutex);
}

static int open_audio_device(struct GroovePlayerPrivate *p) {
    struct GroovePlayer *player = &p->externals;
    int err;

    assert(player->device);
    assert(!p->outstream);

    struct SoundIoDevice *device = player->device;
    p->outstream = soundio_outstream_create(device);
    if (!p->outstream) {
        close_audio_device(p);
        return GrooveErrorNoMem;
    }

    assert(p->audio_buf);
    p->device_format = p->audio_buf->format;

    p->outstream->format = p->device_format.format;
    p->outstream->sample_rate = p->device_format.sample_rate;
    p->outstream->layout = p->device_format.layout;

    p->outstream->userdata = player;
    p->outstream->error_callback = error_callback;
    p->outstream->underflow_callback = underflow_callback;
    p->outstream->write_callback = audio_callback;

    p->outstream->software_latency = 0.025;

    p->outstream->name = player->name;

    p->prebuffering = true;
    if ((err = soundio_outstream_open(p->outstream))) {
        close_audio_device(p);
        av_log(NULL, AV_LOG_ERROR, "unable to open audio device: %s\n", soundio_strerror(err));
        return GrooveErrorOpeningDevice;
    }

    p->device_buffer_frames = ceil(p->outstream->software_latency * (double)p->outstream->sample_rate);

    static const double total_buffer_duration = 2.0;
    double sink_buffer_seconds = groove_max_double(p->outstream->software_latency * 2.0,
            total_buffer_duration - p->outstream->software_latency);

    int buffer_size_bytes = sink_buffer_seconds * p->outstream->sample_rate * p->outstream->bytes_per_frame;
    groove_sink_set_buffer_size_bytes(p->sink, buffer_size_bytes);

    return 0;
}

static void helper_thread_run(void *arg) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) arg;
    int err;

    // This thread's job is to:
    // * Close and re-open the sound device with proper parameters.
    // * Start the outstream when the sink is full.

    groove_os_mutex_lock(p->play_head_mutex);
    while (!p->abort_request) {
        if (p->request_device_close && p->silence_frames_left <= 0) {
            close_audio_device(p);
            emit_event(p->eventq, GROOVE_EVENT_DEVICE_CLOSED);
            p->request_device_close = false;
            p->prebuffering = true;
        }

        if (!p->audio_buf) {
            int ret = groove_sink_buffer_get(p->sink, &p->audio_buf, 0);
            if (ret != GROOVE_BUFFER_YES) {
                groove_os_cond_wait(p->helper_thread_cond, p->play_head_mutex);
                continue;
            }
        }

        bool done_prebuffering = p->prebuffering && (groove_sink_contains_end_of_playlist(p->sink) ||
            groove_sink_get_fill_level(p->sink) >= p->sink->buffer_size_bytes);

        if ((p->request_device_open && p->silence_frames_left <= 0) || (!p->outstream && done_prebuffering)) {
            p->request_device_open = false;
            p->is_started = false;
            if ((err = open_audio_device(p))) {
                emit_event(p->eventq, GROOVE_EVENT_DEVICE_OPEN_ERROR);
                groove_os_mutex_unlock(p->play_head_mutex);
                return;
            }
            emit_event(p->eventq, GROOVE_EVENT_DEVICE_OPENED);
        }

        if (done_prebuffering) {
            p->prebuffering = false;
            if (!p->is_started) {
                p->is_started = true;
                groove_os_mutex_unlock(p->play_head_mutex);
                if ((err = soundio_outstream_start(p->outstream))) {
                    av_log(NULL, AV_LOG_ERROR, "unable to start playback stream: %s\n", soundio_strerror(err));
                    emit_event(p->eventq, GROOVE_EVENT_DEVICE_OPEN_ERROR);
                    groove_os_mutex_unlock(p->play_head_mutex);
                    return;
                }
                groove_os_mutex_lock(p->play_head_mutex);
            }
            soundio_outstream_pause(p->outstream, p->is_paused);
            continue;
        }

        groove_os_cond_wait(p->helper_thread_cond, p->play_head_mutex);
        continue;
    }
    groove_os_mutex_unlock(p->play_head_mutex);

    close_audio_device(p);
}

static void sink_purge(struct GrooveSink *sink, struct GroovePlaylistItem *item) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *)sink->userdata;

    groove_os_mutex_lock(p->play_head_mutex);

    if (p->play_head == item) {
        p->play_head = NULL;
        p->play_pos = -1.0;
        groove_buffer_unref(p->audio_buf);
        p->audio_buf = NULL;
        p->audio_buf_index = 0;
        p->audio_buf_size = 0;
        emit_event(p->eventq, GROOVE_EVENT_NOWPLAYING);
    }

    groove_os_mutex_unlock(p->play_head_mutex);
}

static void sink_pause(struct GrooveSink *sink) {
    struct GroovePlayer *player = (struct GroovePlayer *)sink->userdata;
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;

    groove_os_mutex_lock(p->play_head_mutex);
    set_pause_state(p, true);
    groove_os_mutex_unlock(p->play_head_mutex);
}

static void sink_play(struct GrooveSink *sink) {
    struct GroovePlayer *player = (struct GroovePlayer *)sink->userdata;
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;

    groove_os_mutex_lock(p->play_head_mutex);
    set_pause_state(p, false);
    groove_os_mutex_unlock(p->play_head_mutex);
}

static void sink_filled(struct GrooveSink *sink) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) sink->userdata;

    groove_os_mutex_lock(p->play_head_mutex);
    groove_os_cond_signal(p->helper_thread_cond, p->play_head_mutex);
    groove_os_mutex_unlock(p->play_head_mutex);
}

static void sink_flush(struct GrooveSink *sink) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *)sink->userdata;

    groove_os_mutex_lock(p->play_head_mutex);

    groove_buffer_unref(p->audio_buf);
    p->audio_buf = NULL;
    p->audio_buf_index = 0;
    p->audio_buf_size = 0;
    p->play_pos = -1.0;
    p->play_head = NULL;
    p->prebuffering = true;
    if (p->outstream)
        soundio_outstream_clear_buffer(p->outstream);

    groove_os_mutex_unlock(p->play_head_mutex);
}

struct GroovePlayer *groove_player_create(struct Groove *groove) {
    struct GroovePlayerPrivate *p = ALLOCATE(struct GroovePlayerPrivate, 1);

    if (!p) {
        av_log(NULL, AV_LOG_ERROR, "unable to create player: out of memory\n");
        return NULL;
    }
    struct GroovePlayer *player = &p->externals;

    p->groove = groove;

    p->sink = groove_sink_create(groove);
    if (!p->sink) {
        groove_player_destroy(player);
        av_log(NULL, AV_LOG_ERROR,"unable to create sink: out of memory\n");
        return NULL;
    }

    p->sink->userdata = player;
    p->sink->purge = sink_purge;
    p->sink->flush = sink_flush;

    if (!(p->play_head_mutex = groove_os_mutex_create())) {
        groove_player_destroy(player);
        av_log(NULL, AV_LOG_ERROR,"unable to create play head mutex: out of memory\n");
        return NULL;
    }

    p->eventq = groove_queue_create();
    if (!p->eventq) {
        groove_player_destroy(player);
        av_log(NULL, AV_LOG_ERROR,"unable to create event queue: out of memory\n");
        return NULL;
    }

    if (!(p->helper_thread_cond = groove_os_cond_create())) {
        groove_player_destroy(player);
        av_log(NULL, AV_LOG_ERROR, "unable to create mutex condition\n");
        return NULL;
    }

    // set some nice defaults
    player->gain = p->sink->gain;
    player->name = "libgroove";

    return player;
}

void groove_player_destroy(struct GroovePlayer *player) {
    if (!player)
        return;

    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;

    groove_os_cond_destroy(p->helper_thread_cond);
    groove_os_mutex_destroy(p->play_head_mutex);

    if (p->eventq)
        groove_queue_destroy(p->eventq);

    groove_sink_destroy(p->sink);

    DEALLOCATE(p);
}

static int best_supported_layout(struct SoundIoDevice *device, struct SoundIoChannelLayout *out_layout) {
    for (int i = 0; i < ARRAY_LENGTH(prioritized_layouts); i += 1) {
        enum SoundIoChannelLayoutId layout_id = prioritized_layouts[i];
        const struct SoundIoChannelLayout *layout = soundio_channel_layout_get_builtin(layout_id);
        if (soundio_device_supports_layout(device, layout)) {
            *out_layout = *layout;
            return 0;
        }
    }

    return GrooveErrorDeviceParams;
}

static enum SoundIoFormat best_supported_format(struct SoundIoDevice *device) {
    for (int i = 0; i < ARRAY_LENGTH(prioritized_formats); i += 1) {
        enum SoundIoFormat format = prioritized_formats[i];
        if (soundio_device_supports_format(device, format)) {
            return format;
        }
    }
    return SoundIoFormatInvalid;
}

int groove_player_attach(struct GroovePlayer *player, struct GroovePlaylist *playlist) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;
    int err;

    if (!player->device)
        return GrooveErrorInvalid;
    if (player->device->aim != SoundIoDeviceAimOutput)
        return GrooveErrorInvalid;

    soundio_device_ref(player->device);

    p->sink->gain = player->gain;
    p->sink->pause = sink_pause;
    p->sink->play = sink_play;
    p->sink->filled = sink_filled;

    p->sink->sample_rates = player->device->sample_rates;
    p->sink->sample_rate_count = player->device->sample_rate_count;
    p->sink->sample_rate_default = soundio_device_nearest_sample_rate(player->device, 44100);

    p->sink->channel_layouts = player->device->layouts;
    p->sink->channel_layout_count = player->device->layout_count;
    if ((err = best_supported_layout(player->device, &p->sink->channel_layout_default))) {
        groove_player_detach(player);
        return err;
    }

    p->sink->sample_formats = player->device->formats;
    p->sink->sample_format_count = player->device->format_count;
    p->sink->sample_format_default = best_supported_format(player->device);

    if (p->sink->sample_format_default == SoundIoFormatInvalid) {
        groove_player_detach(player);
        return GrooveErrorDeviceParams;
    }

    p->sink->flags = ((uint32_t)GrooveSinkFlagPlanarOk)|((uint32_t)GrooveSinkFlagInterleavedOk);

    // This is set later when the device is opened.
    // Set to 1 means that it will get exactly one buffer and then consider itself full until
    // we update the buffer size bytes field.
    p->sink->buffer_size_bytes = 1;

    if ((err = groove_sink_attach(p->sink, playlist))) {
        groove_player_detach(player);
        av_log(NULL, AV_LOG_ERROR, "unable to attach sink\n");
        return err;
    }

    p->play_pos = -1.0;
    p->request_device_open = true;
    p->audio_buf_size = 0;
    p->audio_buf_index = 0;
    p->abort_request = false;
    p->silence_frames_left = 0;

    groove_queue_reset(p->eventq);

    assert(!p->outstream);

    set_pause_state(p, !groove_playlist_playing(playlist));

    if ((err = groove_os_thread_create(helper_thread_run, p, &p->helper_thread))) {
        groove_player_detach(player);
        av_log(NULL, AV_LOG_ERROR, "unable to create device thread\n");
        return err;
    }

    return 0;
}

int groove_player_detach(struct GroovePlayer *player) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;

    if (p->helper_thread) {
        groove_os_mutex_lock(p->play_head_mutex);
        p->abort_request = true;
        groove_os_cond_signal(p->helper_thread_cond, p->play_head_mutex);
        groove_os_mutex_unlock(p->play_head_mutex);
        groove_os_thread_destroy(p->helper_thread);
    }

    if (p->eventq) {
        groove_queue_flush(p->eventq);
        groove_queue_abort(p->eventq);
    }
    if (p->sink->playlist) {
        groove_sink_detach(p->sink);
    }

    player->playlist = NULL;

    soundio_device_unref(player->device);
    player->device = NULL;

    groove_buffer_unref(p->audio_buf);
    p->audio_buf = NULL;

    p->abort_request = false;

    return 0;
}

void groove_player_position(struct GroovePlayer *player,
        struct GroovePlaylistItem **item, double *seconds)
{
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;

    groove_os_mutex_lock(p->play_head_mutex);

    if (item)
        *item = p->play_head;

    if (seconds)
        *seconds = p->play_pos - p->play_pos_adjustment;

    groove_os_mutex_unlock(p->play_head_mutex);
}

int groove_player_event_get(struct GroovePlayer *player,
        union GroovePlayerEvent *event, int block)
{
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;
    union GroovePlayerEvent *tmp;
    int err = groove_queue_get(p->eventq, (void **)&tmp, block);
    if (err > 0) {
        *event = *tmp;
        DEALLOCATE(tmp);
    }
    return err;
}

int groove_player_event_peek(struct GroovePlayer *player, int block) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;
    return groove_queue_peek(p->eventq, block);
}

int groove_player_set_gain(struct GroovePlayer *player, double gain) {
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;
    player->gain = gain;
    return groove_sink_set_gain(p->sink, gain);
}

void groove_player_get_device_audio_format(struct GroovePlayer *player,
        struct GrooveAudioFormat *out_audio_format)
{
    struct GroovePlayerPrivate *p = (struct GroovePlayerPrivate *) player;
    groove_os_mutex_lock(p->play_head_mutex);
    *out_audio_format = p->device_format;
    groove_os_mutex_unlock(p->play_head_mutex);
}
