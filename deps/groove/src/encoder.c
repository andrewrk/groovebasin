/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "groove_internal.h"
#include "groove/encoder.h"
#include "queue.h"
#include "buffer.h"
#include "util.h"
#include "atomics.h"

#include <string.h>
#include <pthread.h>

#include <libavformat/avformat.h>
#include <libavcodec/codec.h>

struct GrooveEncoderPrivate {
    struct GrooveEncoder externals;
    struct Groove *groove;
    struct GrooveQueue *audioq;
    struct GrooveSink *sink;
    AVFormatContext *fmt_ctx;
    AVOutputFormat *oformat;
    AVCodec *codec;
    AVStream *stream;
    AVPacket pkt;
    int audioq_size; // in bytes
    struct GrooveAtomicBool abort_request;

    // set temporarily
    struct GroovePlaylistItem *purge_item;

    // encode_head_mutex applies to variables inside this block.
    pthread_mutex_t encode_head_mutex;
    char encode_head_mutex_inited;
    // encode_thread waits on this when the encoded audio buffer queue
    // is full.
    pthread_cond_t drain_cond;
    char drain_cond_inited;
    struct GroovePlaylistItem *encode_head;
    double encode_pos;
    uint64_t encode_pts;

    struct GrooveAudioFormat encode_format;

    pthread_t thread_id;

    AVIOContext *avio;
    unsigned char *avio_buf;

    int sent_header;
    char strbuf[512];

    AVDictionary *metadata;

    uint64_t next_pts;
};

static struct GrooveBuffer *end_of_q_sentinel = NULL;

static enum SoundIoFormat prioritized_sample_formats[] = {
    SoundIoFormatFloat32NE,
    SoundIoFormatS24NE,
    SoundIoFormatS32NE,
    SoundIoFormatS16NE,
    SoundIoFormatFloat64NE,
    SoundIoFormatU8,
};

static int set_codec_ctx_format(AVCodecContext *codec_ctx, const struct GrooveAudioFormat *fmt) {
    switch (fmt->format) {
        case SoundIoFormatU8:
            codec_ctx->bits_per_raw_sample = 8;
            codec_ctx->sample_fmt = fmt->is_planar ? AV_SAMPLE_FMT_U8P : AV_SAMPLE_FMT_U8;
            return 0;
        case SoundIoFormatS16NE:
            codec_ctx->bits_per_raw_sample = 16;
            codec_ctx->sample_fmt = fmt->is_planar ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S16;
            return 0;
        case SoundIoFormatS24NE:
            codec_ctx->bits_per_raw_sample = 24;
            codec_ctx->sample_fmt = fmt->is_planar ? AV_SAMPLE_FMT_S32P : AV_SAMPLE_FMT_S32;
            return 0;
        case SoundIoFormatS32NE:
            codec_ctx->bits_per_raw_sample = 32;
            codec_ctx->sample_fmt = fmt->is_planar ? AV_SAMPLE_FMT_S32P : AV_SAMPLE_FMT_S32;
            return 0;
        case SoundIoFormatFloat32NE:
            codec_ctx->bits_per_raw_sample = 32;
            codec_ctx->sample_fmt = fmt->is_planar ? AV_SAMPLE_FMT_FLTP : AV_SAMPLE_FMT_FLT;
            return 0;
        case SoundIoFormatFloat64NE:
            codec_ctx->bits_per_raw_sample = 64;
            codec_ctx->sample_fmt = fmt->is_planar ? AV_SAMPLE_FMT_DBLP : AV_SAMPLE_FMT_DBL;
            return 0;
        default:
            return GrooveErrorInvalidSampleFormat;
    }
}

static int encode_buffer(struct GrooveEncoder *encoder, struct GrooveBuffer *buffer) {
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;

    av_init_packet(&e->pkt);

    AVFrame *frame = NULL;
    if (buffer) {
        e->encode_head = buffer->item;
        e->encode_pos = buffer->pos;
        e->encode_format = buffer->format;

        struct GrooveBufferPrivate *b = (struct GrooveBufferPrivate *) buffer;
        frame = b->frame;
        frame->pts = e->next_pts;
        e->encode_pts = e->next_pts;
        e->next_pts += buffer->frame_count + 1;
    }

    int got_packet = 0;
    int errcode = avcodec_encode_audio2(e->stream->codec, &e->pkt, frame, &got_packet);
    if (errcode < 0) {
        av_strerror(errcode, e->strbuf, sizeof(e->strbuf));
        av_log(NULL, AV_LOG_ERROR, "error encoding audio frame: %s\n", e->strbuf);
        return GrooveErrorEncoding;
    }
    if (!got_packet)
        return GrooveErrorEncoding;

    av_write_frame(e->fmt_ctx, &e->pkt);
    av_packet_unref(&e->pkt);

    return 0;
}

static void cleanup_avcontext(struct GrooveEncoderPrivate *e) {
    if (e->stream) {
        avcodec_close(e->stream->codec);
        // stream is freed by freeing the AVFormatContext
        e->stream = NULL;
    }

    if (e->fmt_ctx) {
        avformat_free_context(e->fmt_ctx);
        e->fmt_ctx = NULL;
    }

    e->sent_header = 0;
    e->encode_head = NULL;
    e->encode_pos = -1.0;
    e->encode_pts = 0;
    e->next_pts = 0;
}

static int init_avcontext(struct GrooveEncoder *encoder) {
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;
    e->fmt_ctx = avformat_alloc_context();
    if (!e->fmt_ctx) {
        return GrooveErrorNoMem;
    }
    e->fmt_ctx->pb = e->avio;
    e->fmt_ctx->oformat = e->oformat;

    e->stream = avformat_new_stream(e->fmt_ctx, e->codec);
    if (!e->stream) {
        return GrooveErrorNoMem;
    }

    AVCodecContext *codec_ctx = e->stream->codec;
    codec_ctx->bit_rate = encoder->bit_rate;
    set_codec_ctx_format(codec_ctx, &encoder->actual_audio_format);
    codec_ctx->sample_rate = encoder->actual_audio_format.sample_rate;
    codec_ctx->channel_layout = to_ffmpeg_channel_layout(&encoder->actual_audio_format.layout);
    codec_ctx->channels = av_get_channel_layout_nb_channels(codec_ctx->channel_layout);
    codec_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    int err = avcodec_open2(codec_ctx, e->codec, NULL);
    if (err < 0) {
        av_strerror(err, e->strbuf, sizeof(e->strbuf));
        av_log(NULL, AV_LOG_ERROR, "unable to open codec: %s\n", e->strbuf);
        return GrooveErrorEncoding;
    }
    e->stream->codec = codec_ctx;

    return 0;
}

static void *encode_thread(void *arg) {
    struct GrooveEncoder *encoder = (struct GrooveEncoder *)arg;
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;

    struct GrooveBuffer *buffer;
    while (!GROOVE_ATOMIC_LOAD(e->abort_request)) {
        pthread_mutex_lock(&e->encode_head_mutex);

        if (e->audioq_size >= encoder->encoded_buffer_size) {
            pthread_cond_wait(&e->drain_cond, &e->encode_head_mutex);
            pthread_mutex_unlock(&e->encode_head_mutex);
            continue;
        }

        // we definitely want to unlock the mutex while we wait for the
        // next buffer. Otherwise there will be a deadlock when sink_flush or
        // sink_purge is called.
        pthread_mutex_unlock(&e->encode_head_mutex);

        int result = groove_sink_buffer_get(e->sink, &buffer, 1);

        pthread_mutex_lock(&e->encode_head_mutex);

        if (result == GROOVE_BUFFER_END) {
            // flush encoder with empty packets
            while (encode_buffer(encoder, NULL) >= 0) {}
            // then flush format context with empty packets
            while (av_write_frame(e->fmt_ctx, NULL) == 0) {}

            // send trailer
            avio_flush(e->avio);
            av_log(NULL, AV_LOG_INFO, "encoder: writing trailer\n");
            if (av_write_trailer(e->fmt_ctx) < 0) {
                av_log(NULL, AV_LOG_ERROR, "could not write trailer\n");
            }
            avio_flush(e->avio);

            groove_queue_put(e->audioq, end_of_q_sentinel);

            cleanup_avcontext(e);
            init_avcontext(encoder);

            pthread_mutex_unlock(&e->encode_head_mutex);
            continue;
        }

        if (result != GROOVE_BUFFER_YES) {
            pthread_mutex_unlock(&e->encode_head_mutex);
            break;
        }

        if (!e->sent_header) {
            avio_flush(e->avio);

            // copy metadata to format context
            av_dict_free(&e->fmt_ctx->metadata);
            AVDictionaryEntry *tag = NULL;
            while((tag = av_dict_get(e->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
                av_dict_set(&e->fmt_ctx->metadata, tag->key, tag->value, AV_DICT_IGNORE_SUFFIX);
            }

            av_log(NULL, AV_LOG_INFO, "encoder: writing header\n");
            if (avformat_write_header(e->fmt_ctx, NULL) < 0) {
                av_log(NULL, AV_LOG_ERROR, "could not write header\n");
            }
            avio_flush(e->avio);
            e->sent_header = 1;
        }

        encode_buffer(encoder, buffer);
        pthread_mutex_unlock(&e->encode_head_mutex);
        groove_buffer_unref(buffer);
    }
    return NULL;
}

static void sink_purge(struct GrooveSink *sink, struct GroovePlaylistItem *item) {
    struct GrooveEncoder *encoder = (struct GrooveEncoder *)sink->userdata;
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;

    pthread_mutex_lock(&e->encode_head_mutex);
    e->purge_item = item;
    groove_queue_purge(e->audioq);
    e->purge_item = NULL;

    if (e->encode_head == item) {
        e->encode_head = NULL;
        e->encode_pos = -1.0;
    }
    pthread_cond_signal(&e->drain_cond);
    pthread_mutex_unlock(&e->encode_head_mutex);
}

static void sink_flush(struct GrooveSink *sink) {
    struct GrooveEncoder *encoder = (struct GrooveEncoder *)sink->userdata;
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;

    pthread_mutex_lock(&e->encode_head_mutex);
    groove_queue_flush(e->audioq);

    cleanup_avcontext(e);
    init_avcontext(encoder);
    groove_queue_put(e->audioq, end_of_q_sentinel);

    pthread_cond_signal(&e->drain_cond);
    pthread_mutex_unlock(&e->encode_head_mutex);
}

static int audioq_purge(struct GrooveQueue* queue, void *obj) {
    struct GrooveBuffer *buffer = (struct GrooveBuffer *)obj;
    if (buffer == end_of_q_sentinel)
        return 0;
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *)queue->context;
    return buffer->item == e->purge_item;
}

static void audioq_cleanup(struct GrooveQueue* queue, void *obj) {
    struct GrooveBuffer *buffer = (struct GrooveBuffer *)obj;
    if (buffer == end_of_q_sentinel)
        return;
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *)queue->context;
    e->audioq_size -= buffer->size;
    groove_buffer_unref(buffer);
}

static void audioq_put(struct GrooveQueue *queue, void *obj) {
    struct GrooveBuffer *buffer = (struct GrooveBuffer *)obj;
    if (buffer == end_of_q_sentinel)
        return;
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *)queue->context;
    e->audioq_size += buffer->size;
}

static void audioq_get(struct GrooveQueue *queue, void *obj) {
    struct GrooveBuffer *buffer = (struct GrooveBuffer *)obj;
    if (buffer == end_of_q_sentinel)
        return;
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *)queue->context;
    struct GrooveEncoder *encoder = &e->externals;
    e->audioq_size -= buffer->size;

    if (e->audioq_size < encoder->encoded_buffer_size)
        pthread_cond_signal(&e->drain_cond);
}

static int encoder_write_packet(void *opaque, uint8_t *buf, int buf_size) {
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *)opaque;

    struct GrooveBufferPrivate *b = ALLOCATE(struct GrooveBufferPrivate, 1);

    if (!b) {
        return GrooveErrorNoMem;
    }

    struct GrooveBuffer *buffer = &b->externals;

    if (pthread_mutex_init(&b->mutex, NULL)) {
        DEALLOCATE(b);
        return GrooveErrorSystemResources;
    }

    buffer->item = e->encode_head;
    buffer->pos = e->encode_pos;
    buffer->pts = e->encode_pts;
    buffer->format = e->encode_format;

    b->is_packet = 1;
    b->data = ALLOCATE_NONZERO(uint8_t, buf_size);
    if (!b->data) {
        DEALLOCATE(buffer);
        DEALLOCATE(b);
        pthread_mutex_destroy(&b->mutex);
        return GrooveErrorNoMem;
    }
    memcpy(b->data, buf, buf_size);

    buffer->data = &b->data;
    buffer->size = buf_size;

    b->ref_count = 1;

    groove_queue_put(e->audioq, buffer);

    return 0;
}

struct GrooveEncoder *groove_encoder_create(struct Groove *groove) {
    struct GrooveEncoderPrivate *e = ALLOCATE(struct GrooveEncoderPrivate, 1);

    if (!e) {
        av_log(NULL, AV_LOG_ERROR, "unable to allocate encoder\n");
        return NULL;
    }
    struct GrooveEncoder *encoder = &e->externals;

    e->groove = groove;
    GROOVE_ATOMIC_STORE(e->abort_request, false);

    const int buffer_size = 4 * 1024;
    e->avio_buf = ALLOCATE_NONZERO(unsigned char, buffer_size);
    if (!e->avio_buf) {
        groove_encoder_destroy(encoder);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate avio buffer\n");
        return NULL;
    }

    e->avio = avio_alloc_context(e->avio_buf, buffer_size, 1, encoder, NULL,
            encoder_write_packet, NULL);
    if (!e->avio) {
        groove_encoder_destroy(encoder);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate avio context\n");
        return NULL;
    }

    if (pthread_mutex_init(&e->encode_head_mutex, NULL) != 0) {
        groove_encoder_destroy(encoder);
        av_log(NULL, AV_LOG_ERROR, "unable to create mutex\n");
        return NULL;
    }
    e->encode_head_mutex_inited = 1;

    if (pthread_cond_init(&e->drain_cond, NULL) != 0) {
        groove_encoder_destroy(encoder);
        av_log(NULL, AV_LOG_ERROR, "unable to create mutex condition\n");
        return NULL;
    }
    e->drain_cond_inited = 1;

    e->audioq = groove_queue_create();
    if (!e->audioq) {
        groove_encoder_destroy(encoder);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate queue\n");
        return NULL;
    }
    e->audioq->context = encoder;
    e->audioq->cleanup = audioq_cleanup;
    e->audioq->put = audioq_put;
    e->audioq->get = audioq_get;
    e->audioq->purge = audioq_purge;

    e->sink = groove_sink_create(groove);
    if (!e->sink) {
        groove_encoder_destroy(encoder);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate sink\n");
        return NULL;
    }

    e->sink->userdata = encoder;
    e->sink->purge = sink_purge;
    e->sink->flush = sink_flush;

    // set some defaults
    encoder->bit_rate = 256 * 1000;
    encoder->target_audio_format.sample_rate = 44100;
    encoder->target_audio_format.format = SoundIoFormatS16NE;
    encoder->target_audio_format.is_planar = false;
    encoder->target_audio_format.layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
    encoder->sink_buffer_size_bytes = e->sink->buffer_size_bytes;
    encoder->encoded_buffer_size = 16 * 1024;
    encoder->gain = e->sink->gain;

    return encoder;
}

void groove_encoder_destroy(struct GrooveEncoder *encoder) {
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;

    if (e->sink)
        groove_sink_destroy(e->sink);

    if (e->audioq)
        groove_queue_destroy(e->audioq);

    if (e->encode_head_mutex_inited)
        pthread_mutex_destroy(&e->encode_head_mutex);

    if (e->drain_cond_inited)
        pthread_cond_destroy(&e->drain_cond);

    if (e->avio)
        av_free(e->avio);

    if (e->avio_buf)
        DEALLOCATE(e->avio_buf);

    if (e->metadata)
        av_dict_free(&e->metadata);

    DEALLOCATE(e);
}

static int abs_diff(int a, int b) {
    int n = a - b;
    return n >= 0 ? n : -n;
}

static bool codec_supports_fmt(AVCodec *codec, enum SoundIoFormat fmt, bool is_planar) {
    if (!codec->sample_fmts)
        return true;

    const enum AVSampleFormat *p = (enum AVSampleFormat*) codec->sample_fmts;

    if (is_planar) {
        while (*p != AV_SAMPLE_FMT_NONE) {
            if (fmt == SoundIoFormatU8 && *p == AV_SAMPLE_FMT_U8P)
                return true;
            if (fmt == SoundIoFormatS16NE && *p == AV_SAMPLE_FMT_S16P)
                return true;
            if (fmt == SoundIoFormatS24NE && *p == AV_SAMPLE_FMT_S32P)
                return true;
            if (fmt == SoundIoFormatS32NE && *p == AV_SAMPLE_FMT_S32P)
                return true;
            if (fmt == SoundIoFormatFloat32NE && *p == AV_SAMPLE_FMT_FLTP)
                return true;
            if (fmt == SoundIoFormatFloat64NE && *p == AV_SAMPLE_FMT_DBLP)
                return true;
            p += 1;
        }
    } else {
        while (*p != AV_SAMPLE_FMT_NONE) {
            if (fmt == SoundIoFormatU8 && *p == AV_SAMPLE_FMT_U8)
                return true;
            if (fmt == SoundIoFormatS16NE && *p == AV_SAMPLE_FMT_S16)
                return true;
            if (fmt == SoundIoFormatS24NE && *p == AV_SAMPLE_FMT_S32)
                return true;
            if (fmt == SoundIoFormatS32NE && *p == AV_SAMPLE_FMT_S32)
                return true;
            if (fmt == SoundIoFormatFloat32NE && *p == AV_SAMPLE_FMT_FLT)
                return true;
            if (fmt == SoundIoFormatFloat64NE && *p == AV_SAMPLE_FMT_DBL)
                return true;
            p += 1;
        }
    }

    return false;
}

static int closest_supported_sample_fmt(AVCodec *codec, const struct GrooveAudioFormat *target_fmt,
        struct GrooveAudioFormat *actual_fmt)
{
    if (codec_supports_fmt(codec, target_fmt->format, target_fmt->is_planar)) {
        actual_fmt->format = target_fmt->format;
        actual_fmt->is_planar = target_fmt->is_planar;
        return 0;
    }
    if (codec_supports_fmt(codec, target_fmt->format, !target_fmt->is_planar)) {
        actual_fmt->format = target_fmt->format;
        actual_fmt->is_planar = !target_fmt->is_planar;
        return 0;
    }

    for (int i = 0; i < ARRAY_LENGTH(prioritized_sample_formats); i += 1) {
        enum SoundIoFormat fmt = prioritized_sample_formats[i];
        if (codec_supports_fmt(codec, fmt, target_fmt->is_planar)) {
            actual_fmt->format = fmt;
            actual_fmt->is_planar = target_fmt->is_planar;
            return 0;
        }
        if (codec_supports_fmt(codec, fmt, !target_fmt->is_planar)) {
            actual_fmt->format = fmt;
            actual_fmt->is_planar = !target_fmt->is_planar;
            return 0;
        }
    }

    return GrooveErrorInvalidSampleFormat;
}

static int closest_supported_sample_rate(AVCodec *codec, int target) {
    // if we can, return exact match. otherwise, return the minimum sample
    // rate >= target

    if (!codec->supported_samplerates)
        return target;

    const int *p = codec->supported_samplerates;
    int best = *p;

    while (*p) {
        if (*p == target)
            return target;

        if ((best < target && *p > best) || (*p >= target &&
                    abs_diff(target, *p) < abs_diff(target, best)))
        {
            best = *p;
        }

        p += 1;
    }

    return best;
}

static uint64_t closest_supported_channel_layout(AVCodec *codec, uint64_t target) {
    int target_count = av_get_channel_layout_nb_channels(target);
    const uint64_t *p = codec->channel_layouts;
    if (!p)
        return target;
    uint64_t best = *p;
    int best_count = av_get_channel_layout_nb_channels(best);
    while (*p) {
        if (*p == target)
            return target;

        int count = av_get_channel_layout_nb_channels(*p);
        if ((best_count < target_count && count > best_count) ||
            (count >= target_count &&
             abs_diff(target_count, count) < abs_diff(target_count, best_count)))
        {
            best_count = count;
            best = *p;
        }

        p += 1;
    }

    return best;
}

static void log_audio_fmt(const struct GrooveAudioFormat *fmt) {
    const char *channel_layout_str = fmt->layout.name ? fmt->layout.name : "custom layout";
    av_log(NULL, AV_LOG_INFO, "encoder: using audio format: %s, %d Hz, %s\n",
            soundio_format_string(fmt->format), fmt->sample_rate, channel_layout_str);
}

int groove_encoder_attach(struct GrooveEncoder *encoder, struct GroovePlaylist *playlist) {
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;

    encoder->playlist = playlist;
    groove_queue_reset(e->audioq);

    e->oformat = av_guess_format(encoder->format_short_name,
            encoder->filename, encoder->mime_type);
    if (!e->oformat) {
        groove_encoder_detach(encoder);
        return GrooveErrorUnknownFormat;
    }

    // av_guess_codec ignores mime_type, filename, and codec_short_name. see
    // https://trac.ffmpeg.org/ticket/4706
    // because of this we do a workaround to return the correct codec based on
    // the codec_short_name.
    AVCodec *codec = NULL;
    if (encoder->codec_short_name) {
        codec = avcodec_find_encoder_by_name(encoder->codec_short_name);
        if (!codec) {
            const AVCodecDescriptor *desc =
                avcodec_descriptor_get_by_name(encoder->codec_short_name);
            if (desc) {
                codec = avcodec_find_encoder(desc->id);
            }
        }
    }
    if (!codec) {
        enum AVCodecID codec_id = av_guess_codec(e->oformat,
                encoder->codec_short_name, encoder->filename, encoder->mime_type,
                AVMEDIA_TYPE_AUDIO);
        codec = avcodec_find_encoder(codec_id);
        if (!codec) {
            groove_encoder_detach(encoder);
            return GrooveErrorEncoderNotFound;
        }
    }
    e->codec = codec;
    av_log(NULL, AV_LOG_INFO, "encoder: using codec: %s\n", codec->long_name);

    int err;
    if ((err = closest_supported_sample_fmt(codec,
                    &encoder->target_audio_format, &encoder->actual_audio_format)))
    {
        groove_encoder_detach(encoder);
        av_log(NULL, AV_LOG_ERROR, "no sample format supported\n");
        return err;
    }

    encoder->actual_audio_format.sample_rate = closest_supported_sample_rate(
            codec, encoder->target_audio_format.sample_rate);

    uint64_t target_channel_layout = to_ffmpeg_channel_layout(&encoder->target_audio_format.layout);
    uint64_t out_channel_layout = closest_supported_channel_layout(codec, target_channel_layout);
    from_ffmpeg_layout(out_channel_layout, &encoder->actual_audio_format.layout);

    log_audio_fmt(&encoder->actual_audio_format);

    if ((err = init_avcontext(encoder)) < 0) {
        groove_encoder_detach(encoder);
        return err;
    }

    groove_sink_set_only_format(e->sink, &encoder->actual_audio_format);
    e->sink->buffer_size_bytes = encoder->sink_buffer_size_bytes;
    e->sink->buffer_sample_count = (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) ?
        0 : e->stream->codec->frame_size;
    e->sink->gain = encoder->gain;

    if ((err = groove_sink_attach(e->sink, playlist))) {
        groove_encoder_detach(encoder);
        return err;
    }

    if (pthread_create(&e->thread_id, NULL, encode_thread, encoder)) {
        groove_encoder_detach(encoder);
        return GrooveErrorSystemResources;
    }

    return 0;
}

int groove_encoder_detach(struct GrooveEncoder *encoder) {
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;

    GROOVE_ATOMIC_STORE(e->abort_request, true);
    groove_sink_detach(e->sink);
    groove_queue_flush(e->audioq);
    groove_queue_abort(e->audioq);
    pthread_cond_signal(&e->drain_cond);
    pthread_join(e->thread_id, NULL);
    GROOVE_ATOMIC_STORE(e->abort_request, false);

    cleanup_avcontext(e);
    e->oformat = NULL;
    e->codec = NULL;

    encoder->playlist = NULL;
    return 0;
}

int groove_encoder_buffer_get(struct GrooveEncoder *encoder,
        struct GrooveBuffer **buffer, int block)
{
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;

    if (groove_queue_get(e->audioq, (void**)buffer, block) == 1) {
        if (*buffer == end_of_q_sentinel) {
            *buffer = NULL;
            return GROOVE_BUFFER_END;
        } else {
            return GROOVE_BUFFER_YES;
        }
    } else {
        *buffer = NULL;
        return GROOVE_BUFFER_NO;
    }
}

struct GrooveTag *groove_encoder_metadata_get(struct GrooveEncoder *encoder, const char *key,
        const struct GrooveTag *prev, int flags)
{
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;
    const AVDictionaryEntry *entry = (const AVDictionaryEntry *) prev;
    return (struct GrooveTag *) av_dict_get(e->metadata, key, entry, flags|AV_DICT_IGNORE_SUFFIX);
}

int groove_encoder_metadata_set(struct GrooveEncoder *encoder, const char *key,
        const char *value, int flags)
{
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;
    return av_dict_set(&e->metadata, key, value, flags|AV_DICT_IGNORE_SUFFIX);
}

int groove_encoder_buffer_peek(struct GrooveEncoder *encoder, int block) {
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;
    return groove_queue_peek(e->audioq, block);
}

void groove_encoder_position(struct GrooveEncoder *encoder,
        struct GroovePlaylistItem **item, double *seconds)
{
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;

    pthread_mutex_lock(&e->encode_head_mutex);

    if (item)
        *item = e->encode_head;

    if (seconds)
        *seconds = e->encode_pos;

    pthread_mutex_unlock(&e->encode_head_mutex);
}

int groove_encoder_set_gain(struct GrooveEncoder *encoder, double gain) {
    struct GrooveEncoderPrivate *e = (struct GrooveEncoderPrivate *) encoder;
    encoder->gain = gain;
    return groove_sink_set_gain(e->sink, gain);
}
