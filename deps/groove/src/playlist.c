/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "file.h"
#include "queue.h"
#include "buffer.h"
#include "util.h"
#include "atomics.h"

#define __STDC_FORMAT_MACROS
#include <pthread.h>
#include <inttypes.h>

#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/samplefmt.h>

struct GrooveSinkPrivate {
    struct GrooveSink externals;
    struct Groove *groove;
    struct GrooveQueue *audioq;
    struct GrooveAtomicInt audioq_size; // in bytes
    int min_audioq_size; // in bytes
    struct GrooveAtomicBool audioq_contains_end;
    struct SoundIoSampleRateRange prealloc_sample_rate_range;
};

struct SinkStack {
    struct GrooveSink *sink;
    struct SinkStack *next;
};

struct SinkMap {
    struct SinkStack *stack_head;
    AVFilterContext *abuffersink_ctx;
    struct SinkMap *next;
};

struct GroovePlaylistPrivate {
    struct GroovePlaylist externals;
    struct Groove *groove;
    pthread_t thread_id;
    bool thread_inited;
    bool abort_request;

    AVPacket audio_pkt_temp;
    AVFrame *in_frame;
    struct GrooveAtomicBool paused;

    int in_sample_rate;
    uint64_t in_channel_layout;
    enum AVSampleFormat in_sample_fmt;
    AVRational in_time_base;

    char strbuf[512];
    AVFilterGraph *filter_graph;
    AVFilterContext *abuffer_ctx;

    AVFilter *volume_filter;
    AVFilter *compand_filter;
    AVFilter *abuffer_filter;
    AVFilter *asplit_filter;
    AVFilter *aformat_filter;
    AVFilter *abuffersink_filter;

    pthread_mutex_t drain_cond_mutex;
    int drain_cond_mutex_inited;

    // this mutex applies to the variables in this block
    pthread_mutex_t decode_head_mutex;
    int decode_head_mutex_inited;
    // decode_thread waits on this cond when the decode_head is NULL
    pthread_cond_t decode_head_cond;
    int decode_head_cond_inited;
    // decode_thread waits on this cond when every sink is full
    // should also signal when the first sink is attached.
    pthread_cond_t sink_drain_cond;
    int sink_drain_cond_inited;
    // pointer to current playlist item being decoded
    struct GroovePlaylistItem *decode_head;
    // desired volume for the volume filter
    double volume;
    // known true peak value
    double peak;
    // set to 1 to trigger a rebuild
    int rebuild_filter_graph_flag;
    // map audio format to list of sinks
    // for each map entry, use the first sink in the stack as the example
    // of the audio format in that stack
    struct SinkMap *sink_map;
    int sink_map_count;

    // the value that was used to construct the filter graph
    double filter_volume;
    double filter_peak;

    // only touched by decode_thread, tells whether we have sent the end_of_q_sentinel
    int sent_end_of_q;

    struct GroovePlaylistItem *purge_item; // set temporarily

    int (*detect_full_sinks)(struct GroovePlaylist*);
};

// this is used to tell the difference between a buffer underrun
// and the end of the playlist.
static struct GrooveBuffer *end_of_q_sentinel = NULL;

static int frame_size(const AVFrame *frame) {
    return av_get_channel_layout_nb_channels(frame->channel_layout) *
        av_get_bytes_per_sample((enum AVSampleFormat)frame->format) * frame->nb_samples;
}

static struct GrooveBuffer *frame_to_groove_buffer(struct GroovePlaylist *playlist,
        struct GrooveSink *sink, AVFrame *frame)
{
    struct GrooveBufferPrivate *b = ALLOCATE(struct GrooveBufferPrivate, 1);

    if (!b)
        return NULL;

    struct GrooveBuffer *buffer = &b->externals;

    if (pthread_mutex_init(&b->mutex, NULL) != 0) {
        DEALLOCATE(b);
        return NULL;
    }

    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    struct GrooveFile *file = p->decode_head->file;

    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    buffer->item = p->decode_head;
    buffer->pos = f->audio_clock;

    buffer->data = frame->extended_data;
    buffer->frame_count = frame->nb_samples;
    from_ffmpeg_layout(frame->channel_layout, &buffer->format.layout);
    buffer->format.format = from_ffmpeg_format((enum AVSampleFormat)frame->format);
    buffer->format.is_planar = from_ffmpeg_format_planar((enum AVSampleFormat)frame->format);
    buffer->format.sample_rate = frame->sample_rate;
    buffer->size = frame_size(frame);
    buffer->pts = frame->pts;

    b->frame = frame;

    return buffer;
}


// decode one audio packet and return its uncompressed size
static int audio_decode_frame(struct GroovePlaylist *playlist, struct GrooveFile *file) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    AVPacket *pkt = &f->audio_pkt;
    AVCodecContext *dec = f->audio_st->codec;

    AVPacket *pkt_temp = &p->audio_pkt_temp;
    *pkt_temp = *pkt;

    // update the audio clock with the pts if we can
    if (pkt->pts != AV_NOPTS_VALUE)
        f->audio_clock = av_q2d(f->audio_st->time_base) * pkt->pts;

    int max_data_size = 0;
    int len1, got_frame;
    int new_packet = 1;
    AVFrame *in_frame = p->in_frame;

    // NOTE: the audio packet can contain several frames
    while (pkt_temp->size > 0 || (!pkt_temp->data && new_packet)) {
        new_packet = 0;

        len1 = avcodec_decode_audio4(dec, in_frame, &got_frame, pkt_temp);
        if (len1 < 0) {
            // if error, we skip the frame
            pkt_temp->size = 0;
            return -1;
        }

        pkt_temp->data += len1;
        pkt_temp->size -= len1;

        if (!got_frame) {
            // stop sending empty packets if the decoder is finished
            if (!pkt_temp->data && dec->codec->capabilities & AV_CODEC_CAP_DELAY)
                return 0;
            continue;
        }

        // push the audio data from decoded frame into the filtergraph
        int err = av_buffersrc_write_frame(p->abuffer_ctx, in_frame);
        if (err < 0) {
            av_strerror(err, p->strbuf, sizeof(p->strbuf));
            av_log(NULL, AV_LOG_ERROR, "error writing frame to buffersrc: %s\n",
                    p->strbuf);
            if (err == AVERROR(ENOMEM)) {
                return GrooveErrorNoMem;
            } else {
                return GrooveErrorDecoding;
            }
        }

        // for each data format in the sink map, pull filtered audio from its
        // buffersink, turn it into a GrooveBuffer and then increment the ref
        // count for each sink in that stack.
        struct SinkMap *map_item = p->sink_map;
        double clock_adjustment = 0;
        while (map_item) {
            struct GrooveSink *example_sink = map_item->stack_head->sink;
            int data_size = 0;
            for (;;) {
                AVFrame *oframe = av_frame_alloc();
                int err = example_sink->buffer_sample_count == 0 ?
                    av_buffersink_get_frame(map_item->abuffersink_ctx, oframe) :
                    av_buffersink_get_samples(map_item->abuffersink_ctx, oframe, example_sink->buffer_sample_count);
                if (err == AVERROR_EOF || err == AVERROR(EAGAIN)) {
                    av_frame_free(&oframe);
                    break;
                }
                if (err < 0) {
                    av_frame_free(&oframe);
                    av_log(NULL, AV_LOG_ERROR, "error reading buffer from buffersink\n");
                    return GrooveErrorDecoding;
                }
                struct GrooveBuffer *buffer = frame_to_groove_buffer(playlist, example_sink, oframe);
                if (!buffer) {
                    av_frame_free(&oframe);
                    return GrooveErrorNoMem;
                }
                if (!clock_adjustment && pkt->pts == AV_NOPTS_VALUE) {
                    double bytes_per_sec = soundio_get_bytes_per_second(
                            buffer->format.format, buffer->format.layout.channel_count,
                            buffer->format.sample_rate);
                    clock_adjustment = buffer->size / bytes_per_sec;
                }
                data_size += buffer->size;
                struct SinkStack *stack_item = map_item->stack_head;
                // we hold this reference to avoid cleanups until at least this loop
                // is done and we call unref after it.
                groove_buffer_ref(buffer);
                while (stack_item) {
                    struct GrooveSink *sink = stack_item->sink;
                    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;
                    // as soon as we call groove_queue_put, this buffer could be unref'd.
                    // so we ref before putting it in the queue, and unref if it failed.
                    groove_buffer_ref(buffer);
                    if (groove_queue_put(s->audioq, buffer) < 0) {
                        av_log(NULL, AV_LOG_ERROR, "unable to put buffer in queue\n");
                        groove_buffer_unref(buffer);
                    }
                    if (sink->filled) sink->filled(sink);
                    stack_item = stack_item->next;
                }
                groove_buffer_unref(buffer);
            }
            max_data_size = groove_max_int(max_data_size, data_size);
            map_item = map_item->next;
        }

        // if no pts, then estimate it
        if (pkt->pts == AV_NOPTS_VALUE)
            f->audio_clock += clock_adjustment;
        return max_data_size;
    }
    return max_data_size;
}

static const double dB_scale = 0.1151292546497023; // log(10) * 0.05

static double gain_to_dB(double gain) {
    return log(gain) / dB_scale;
}

static int create_volume_filter(struct GroovePlaylistPrivate *p, AVFilterContext **audio_src_ctx,
        double vol, double amp_vol)
{
    int err;

    if (vol < 0.0) vol = 0.0;
    if (amp_vol < 1.0) {
        snprintf(p->strbuf, sizeof(p->strbuf), "volume=%f", vol);
        av_log(NULL, AV_LOG_INFO, "volume: %s\n", p->strbuf);
        AVFilterContext *volume_ctx;
        err = avfilter_graph_create_filter(&volume_ctx, p->volume_filter, NULL,
                p->strbuf, NULL, p->filter_graph);
        if (err < 0) {
            av_log(NULL, AV_LOG_ERROR, "error initializing volume filter\n");
            return err;
        }
        err = avfilter_link(*audio_src_ctx, 0, volume_ctx, 0);
        if (err < 0) {
            av_strerror(err, p->strbuf, sizeof(p->strbuf));
            av_log(NULL, AV_LOG_ERROR, "unable to link volume filter: %s\n", p->strbuf);
            return err;
        }
        *audio_src_ctx = volume_ctx;
    } else if (amp_vol > 1.0) {
        double attack = 0.1;
        double decay = 0.2;
        const char *points = "-2/-2";
        double soft_knee = 0.02;
        double gain = gain_to_dB(vol);
        double volume_param = 0.0;
        double delay = 0.2;
        snprintf(p->strbuf, sizeof(p->strbuf), "%f:%f:%s:%f:%f:%f:%f",
                attack, decay, points, soft_knee, gain, volume_param, delay);
        av_log(NULL, AV_LOG_INFO, "compand: %s\n", p->strbuf);
        AVFilterContext *compand_ctx;
        err = avfilter_graph_create_filter(&compand_ctx, p->compand_filter, NULL,
                p->strbuf, NULL, p->filter_graph);
        if (err < 0) {
            av_log(NULL, AV_LOG_ERROR, "error initializing compand filter\n");
            return err;
        }
        err = avfilter_link(*audio_src_ctx, 0, compand_ctx, 0);
        if (err < 0) {
            av_strerror(err, p->strbuf, sizeof(p->strbuf));
            av_log(NULL, AV_LOG_ERROR, "unable to link compand filter: %s\n", p->strbuf);
            return err;
        }
        *audio_src_ctx = compand_ctx;
    }
    return 0;
}

// abuffer -> volume -> asplit for each audio format
//                     -> volume -> aformat -> abuffersink
// if the volume gain is > 1.0, we use a compand filter instead
// for soft limiting.
static int init_filter_graph(struct GroovePlaylist *playlist, struct GrooveFile *file) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    // destruct old graph
    avfilter_graph_free(&p->filter_graph);

    // create new graph
    p->filter_graph = avfilter_graph_alloc();
    if (!p->filter_graph) {
        av_log(NULL, AV_LOG_ERROR, "unable to create filter graph: out of memory\n");
        return -1;
    }

    int err;
    // create abuffer filter
    AVCodecContext *avctx = f->audio_st->codec;
    AVRational time_base = f->audio_st->time_base;
    snprintf(p->strbuf, sizeof(p->strbuf),
            "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64, 
            time_base.num, time_base.den, avctx->sample_rate,
            av_get_sample_fmt_name(avctx->sample_fmt),
            avctx->channel_layout);
    av_log(NULL, AV_LOG_INFO, "abuffer: %s\n", p->strbuf);
    // save these values so we can compare later and check
    // whether we have to reconstruct the graph
    p->in_sample_rate = avctx->sample_rate;
    p->in_channel_layout = avctx->channel_layout;
    p->in_sample_fmt = avctx->sample_fmt;
    p->in_time_base = time_base;
    err = avfilter_graph_create_filter(&p->abuffer_ctx, p->abuffer_filter,
            NULL, p->strbuf, NULL, p->filter_graph);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "error initializing abuffer filter\n");
        return err;
    }
    // as we create filters, this points the next source to link to
    AVFilterContext *audio_src_ctx = p->abuffer_ctx;

    // save the volume value so we can compare later and check
    // whether we have to reconstruct the graph
    p->filter_volume = p->volume;
    p->filter_peak = p->peak;
    // if volume is < 1.0, create volume filter
    //             == 1.0, do not create a filter
    //              > 1.0, create a compand filter (for soft limiting)
    double vol = p->volume;
    // adjust for the known true peak of the playlist item. In other words, if
    // we know that the song peaks at 0.8, and we want to amplify by 1.2, that
    // comes out to 0.96 so we know that we can safely amplify by 1.2 even
    // though it's greater than 1.0.
    double amp_vol = vol * (p->peak > 1.0 ? 1.0 : p->peak);
    err = create_volume_filter(p, &audio_src_ctx, vol, amp_vol);
    if (err < 0)
        return err;

    // if only one sink, no need for asplit
    if (p->sink_map_count >= 2) {
        AVFilterContext *asplit_ctx;
        snprintf(p->strbuf, sizeof(p->strbuf), "%d", p->sink_map_count);
        av_log(NULL, AV_LOG_INFO, "asplit: %s\n", p->strbuf);
        err = avfilter_graph_create_filter(&asplit_ctx, p->asplit_filter,
                NULL, p->strbuf, NULL, p->filter_graph);
        if (err < 0) {
            av_log(NULL, AV_LOG_ERROR, "unable to create asplit filter\n");
            return err;
        }
        err = avfilter_link(audio_src_ctx, 0, asplit_ctx, 0);
        if (err < 0) {
            av_log(NULL, AV_LOG_ERROR, "unable to link to asplit\n");
            return err;
        }
        audio_src_ctx = asplit_ctx;
    }

    // for each audio format, create aformat and abuffersink filters
    struct SinkMap *map_item = p->sink_map;
    int pad_index = 0;
    while (map_item) {
        struct GrooveSink *example_sink = map_item->stack_head->sink;

        AVFilterContext *inner_audio_src_ctx = audio_src_ctx;

        // create volume filter
        err = create_volume_filter(p, &inner_audio_src_ctx, example_sink->gain, example_sink->gain);
        if (err < 0)
            return err;

        // Create aformat filter if and only if the sink is compatible with
        // the input format.
        bool need_aformat = false;

        // Check for planar vs interleaved.
        bool is_planar = from_ffmpeg_format_planar(avctx->sample_fmt);
        bool aformat_planar = is_planar;
        bool planar_ok = (example_sink->flags & GrooveSinkFlagPlanarOk);
        bool interleaved_ok = (example_sink->flags & GrooveSinkFlagInterleavedOk);
        if (!planar_ok && !interleaved_ok) {
            planar_ok = true;
            interleaved_ok = true;
        }
        if (is_planar && !planar_ok) {
            aformat_planar = false;
            need_aformat = true;
        } else if (!is_planar && !interleaved_ok) {
            aformat_planar = true;
            need_aformat = true;
        }

        // Check for sample rate.
        int aformat_sample_rate = avctx->sample_rate;
        bool sample_rate_ok = false;
        if (example_sink->sample_rates) {
            for (int i = 0; i < example_sink->sample_rate_count; i += 1) {
                struct SoundIoSampleRateRange *range = &example_sink->sample_rates[i];
                if (range->min <= avctx->sample_rate && avctx->sample_rate <= range->max) {
                    sample_rate_ok = true;
                    break;
                }
            }
        } else {
            sample_rate_ok = true;
        }
        if (!sample_rate_ok) {
            aformat_sample_rate = example_sink->sample_rate_default;
            need_aformat = true;
        }

        // Check for channel layout.
        struct SoundIoChannelLayout aformat_layout;
        from_ffmpeg_layout(avctx->channel_layout, &aformat_layout);
        bool channel_layout_ok = false;
        if (example_sink->channel_layouts) {
            for (int i = 0; i < example_sink->channel_layout_count; i += 1) {
                struct SoundIoChannelLayout *layout = &example_sink->channel_layouts[i];
                if (soundio_channel_layout_equal(layout, &aformat_layout)) {
                    channel_layout_ok = true;
                    break;
                }
            }
        } else {
            channel_layout_ok = true;
        }
        if (!channel_layout_ok) {
            aformat_layout = example_sink->channel_layout_default;
            need_aformat = true;
        }

        // Check for sample format.
        enum SoundIoFormat aformat_format = from_ffmpeg_format(avctx->sample_fmt);
        bool format_ok = false;
        if (example_sink->sample_formats) {
            for (int i = 0; i < example_sink->sample_format_count; i += 1) {
                enum SoundIoFormat format = example_sink->sample_formats[i];
                if (format == aformat_format) {
                    format_ok = true;
                    break;
                }
            }
        } else {
            format_ok = true;
        }
        if (!format_ok) {
            aformat_format = example_sink->sample_format_default;
            need_aformat = true;
        }

        if (need_aformat) {
            AVFilterContext *aformat_ctx;
            // create aformat filter
            snprintf(p->strbuf, sizeof(p->strbuf),
                    "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%" PRIx64,
                    av_get_sample_fmt_name(to_ffmpeg_fmt_params(aformat_format, aformat_planar)),
                    aformat_sample_rate, to_ffmpeg_channel_layout(&aformat_layout));
            av_log(NULL, AV_LOG_INFO, "aformat: %s\n", p->strbuf);
            err = avfilter_graph_create_filter(&aformat_ctx, p->aformat_filter,
                    NULL, p->strbuf, NULL, p->filter_graph);
            if (err < 0) {
                av_strerror(err, p->strbuf, sizeof(p->strbuf));
                av_log(NULL, AV_LOG_ERROR, "unable to create aformat filter: %s\n",
                        p->strbuf);
                return err;
            }
            err = avfilter_link(inner_audio_src_ctx, pad_index, aformat_ctx, 0);
            if (err < 0) {
                av_strerror(err, p->strbuf, sizeof(p->strbuf));
                av_log(NULL, AV_LOG_ERROR, "unable to link aformat filter: %s\n", p->strbuf);
                return err;
            }
            inner_audio_src_ctx = aformat_ctx;
        }

        // create abuffersink filter
        err = avfilter_graph_create_filter(&map_item->abuffersink_ctx, p->abuffersink_filter,
                NULL, NULL, NULL, p->filter_graph);
        if (err < 0) {
            av_log(NULL, AV_LOG_ERROR, "unable to create abuffersink filter\n");
            return err;
        }
        err = avfilter_link(inner_audio_src_ctx, 0, map_item->abuffersink_ctx, 0);
        if (err < 0) {
            av_strerror(err, p->strbuf, sizeof(p->strbuf));
            av_log(NULL, AV_LOG_ERROR, "unable to link abuffersink filter: %s\n", p->strbuf);
            return err;
        }

        pad_index += 1;
        map_item = map_item->next;
    }

    err = avfilter_graph_config(p->filter_graph, NULL);
    if (err < 0) {
        av_strerror(err, p->strbuf, sizeof(p->strbuf));
        av_log(NULL, AV_LOG_ERROR, "error configuring the filter graph: %s\n",
                p->strbuf);
        return err;
    }

    p->rebuild_filter_graph_flag = 0;

    return 0;
}

static int maybe_init_filter_graph(struct GroovePlaylist *playlist, struct GrooveFile *file) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;
    AVCodecContext *avctx = f->audio_st->codec;
    AVRational time_base = f->audio_st->time_base;

    // if the input format stuff has changed, then we need to re-build the graph
    if (!p->filter_graph || p->rebuild_filter_graph_flag ||
        p->in_sample_rate != avctx->sample_rate ||
        p->in_channel_layout != avctx->channel_layout ||
        p->in_sample_fmt != avctx->sample_fmt ||
        p->in_time_base.num != time_base.num ||
        p->in_time_base.den != time_base.den ||
        p->volume != p->filter_volume ||
        p->peak != p->filter_peak)
    {
        return init_filter_graph(playlist, file);
    }

    return 0;
}

static int every_sink(struct GroovePlaylist *playlist, int (*func)(struct GrooveSink *), int default_value) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    struct SinkMap *map_item = p->sink_map;
    while (map_item) {
        struct SinkStack *stack_item = map_item->stack_head;
        while (stack_item) {
            struct GrooveSink *sink = stack_item->sink;
            int value = func(sink);
            if (value != default_value)
                return value;
            stack_item = stack_item->next;
        }
        map_item = map_item->next;
    }
    return default_value;
}

static int sink_is_full(struct GrooveSink *sink) {
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;
    return GROOVE_ATOMIC_LOAD(s->audioq_size) >= s->min_audioq_size;
}

static int every_sink_full(struct GroovePlaylist *playlist) {
    return every_sink(playlist, sink_is_full, 1);
}

static int any_sink_full(struct GroovePlaylist *playlist) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    if (!p->sink_map)
        return 1;
    else
        return every_sink(playlist, sink_is_full, 0);
}

static int sink_signal_end(struct GrooveSink *sink) {
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;
    groove_queue_put(s->audioq, end_of_q_sentinel);
    if (sink->filled) sink->filled(sink);
    return 0;
}

static void every_sink_signal_end(struct GroovePlaylist *playlist) {
    every_sink(playlist, sink_signal_end, 0);
}

static int sink_flush(struct GrooveSink *sink) {
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;

    groove_queue_flush(s->audioq);
    if (sink->flush)
        sink->flush(sink);

    return 0;
}

static void every_sink_flush(struct GroovePlaylist *playlist) {
    every_sink(playlist, sink_flush, 0);
}

static int decode_one_frame(struct GroovePlaylist *playlist, struct GrooveFile *file) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;
    AVPacket *pkt = &f->audio_pkt;

    // abort_request is set if we are destroying the file
    if (GROOVE_ATOMIC_LOAD(f->abort_request))
        return -1;

    // might need to rebuild the filter graph if certain things changed
    if (maybe_init_filter_graph(playlist, file) < 0)
        return -1;

    // handle seek requests
    pthread_mutex_lock(&f->seek_mutex);
    if (f->seek_pos >= 0) {
        if (f->seek_pos != 0 || f->seek_flush || f->ever_seeked) {
            int64_t seek_pos = f->seek_pos;
            if (seek_pos == 0 && f->audio_st->start_time != AV_NOPTS_VALUE)
                seek_pos = f->audio_st->start_time;
            if (av_seek_frame(f->ic, f->audio_stream_index, seek_pos, 0) < 0) {
                av_log(NULL, AV_LOG_ERROR, "%s: error while seeking\n", f->ic->filename);
            } else if (f->seek_flush) {
                every_sink_flush(playlist);
            }
            avcodec_flush_buffers(f->audio_st->codec);
        }
        f->ever_seeked = true;
        f->seek_pos = -1;
        f->eof = 0;
    }
    pthread_mutex_unlock(&f->seek_mutex);

    if (f->eof) {
        if (f->audio_st->codec->codec->capabilities & AV_CODEC_CAP_DELAY) {
            av_init_packet(pkt);
            pkt->data = NULL;
            pkt->size = 0;
            pkt->stream_index = f->audio_stream_index;
            if (audio_decode_frame(playlist, file) > 0) {
                // keep flushing
                return 0;
            }
        }
        // this file is complete. move on
        return -1;
    }
    int err = av_read_frame(f->ic, pkt);
    if (err < 0) {
        // treat all errors as EOF, but log non-EOF errors.
        if (err != AVERROR_EOF) {
            av_log(NULL, AV_LOG_WARNING, "error reading frames\n");
        }
        f->eof = 1;
        return 0;
    }
    if (pkt->stream_index != f->audio_stream_index) {
        // we're only interested in the One True Audio Stream
        av_packet_unref(pkt);
        return 0;
    }
    audio_decode_frame(playlist, file);
    av_packet_unref(pkt);
    return 0;
}

static void audioq_put(struct GrooveQueue *queue, void *obj) {
    struct GrooveBuffer *buffer = (struct GrooveBuffer *)obj;
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *)queue->context;
    if (buffer == end_of_q_sentinel) {
        GROOVE_ATOMIC_STORE(s->audioq_contains_end, true);
    } else {
        GROOVE_ATOMIC_FETCH_ADD(s->audioq_size, buffer->size);
    }
}

static void audioq_get(struct GrooveQueue *queue, void *obj) {
    struct GrooveBuffer *buffer = (struct GrooveBuffer *)obj;
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *)queue->context;
    if (buffer == end_of_q_sentinel) {
        GROOVE_ATOMIC_STORE(s->audioq_contains_end, false);
        return;
    }
    struct GrooveSink *sink = &s->externals;
    GROOVE_ATOMIC_FETCH_ADD(s->audioq_size, -buffer->size);

    struct GroovePlaylist *playlist = sink->playlist;
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    if (GROOVE_ATOMIC_LOAD(s->audioq_size) < s->min_audioq_size) {
        pthread_mutex_lock(&p->drain_cond_mutex);
        pthread_cond_signal(&p->sink_drain_cond);
        pthread_mutex_unlock(&p->drain_cond_mutex);
    }
}

static void audioq_cleanup(struct GrooveQueue *queue, void *obj) {
    struct GrooveBuffer *buffer = (struct GrooveBuffer *)obj;
    struct GrooveSink *sink = (struct GrooveSink *)queue->context;
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;
    if (buffer == end_of_q_sentinel) {
        GROOVE_ATOMIC_STORE(s->audioq_contains_end, false);
        return;
    }
    GROOVE_ATOMIC_FETCH_ADD(s->audioq_size, -buffer->size);
    groove_buffer_unref(buffer);
}

static int audioq_purge(struct GrooveQueue *queue, void *obj) {
    struct GrooveBuffer *buffer = (struct GrooveBuffer *)obj;
    if (buffer == end_of_q_sentinel)
        return 0;
    struct GrooveSink *sink = (struct GrooveSink *)queue->context;
    struct GroovePlaylist *playlist = sink->playlist;
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    struct GroovePlaylistItem *item = p->purge_item;
    return buffer->item == item;
}

static void update_playlist_volume(struct GroovePlaylist *playlist) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    struct GroovePlaylistItem *item = p->decode_head;
    p->volume = playlist->gain * item->gain;
    p->peak = item->peak;
}

// this thread is responsible for decoding and inserting buffers of decoded
// audio into each sink
static void *decode_thread(void *arg) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *)arg;
    struct GroovePlaylist *playlist = &p->externals;

    pthread_mutex_lock(&p->decode_head_mutex);
    while (!p->abort_request) {
        // if we don't have anything to decode, wait until we do
        if (!p->decode_head) {
            if (!p->sent_end_of_q) {
                every_sink_signal_end(playlist);
                p->sent_end_of_q = 1;
            }
            pthread_cond_wait(&p->decode_head_cond, &p->decode_head_mutex);
            continue;
        }
        p->sent_end_of_q = 0;

        // if all sinks are filled up, no need to read more
        struct GrooveFile *file = p->decode_head->file;
        struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

        pthread_mutex_lock(&p->drain_cond_mutex);
        if (p->detect_full_sinks(playlist) && (f->seek_pos < 0 || !f->seek_flush)) {
            if (!f->paused) {
                av_read_pause(f->ic);
                f->paused = 1;
            }
            pthread_mutex_unlock(&p->decode_head_mutex);
            pthread_cond_wait(&p->sink_drain_cond, &p->drain_cond_mutex);
            pthread_mutex_unlock(&p->drain_cond_mutex);
            continue;
        }
        pthread_mutex_unlock(&p->drain_cond_mutex);
        if (f->paused) {
            av_read_play(f->ic);
            f->paused = 0;
        }

        update_playlist_volume(playlist);

        if (decode_one_frame(playlist, file) < 0) {
            p->decode_head = p->decode_head->next;
            // seek to beginning of next song
            if (p->decode_head) {
                struct GrooveFile *next_file = p->decode_head->file;
                struct GrooveFilePrivate *next_f = (struct GrooveFilePrivate *) next_file;
                pthread_mutex_lock(&next_f->seek_mutex);
                next_f->seek_pos = 0;
                next_f->seek_flush = 0;
                pthread_mutex_unlock(&next_f->seek_mutex);
            }
        }

    }
    pthread_mutex_unlock(&p->decode_head_mutex);

    return NULL;
}

static bool sink_supports_sample_rate_range(const struct GrooveSink *test_sink,
        const struct SoundIoSampleRateRange *test_range)
{
    if (!test_sink->sample_rates)
        return true;

    for (int i = 0; i < test_sink->sample_rate_count; i += 1) {
        const struct SoundIoSampleRateRange *range = &test_sink->sample_rates[i];

        if (test_range->min >= range->min && test_range->max <= range->max) {
            return true;
        }
    }
    return false;
}

static bool sink_supports_sample_format(const struct GrooveSink *test_sink, enum SoundIoFormat test_format) {
    if (!test_sink->sample_formats)
        return true;

    for (int i = 0; i < test_sink->sample_format_count; i += 1) {
        enum SoundIoFormat format = test_sink->sample_formats[i];
        if (format == test_format)
            return true;
    }
    return false;
}

static bool sink_supports_channel_layout(const struct GrooveSink *test_sink,
        const struct SoundIoChannelLayout *test_layout)
{
    if (!test_sink->channel_layouts)
        return true;

    for (int i = 0; i < test_sink->channel_layout_count; i += 1) {
        const struct SoundIoChannelLayout *layout = &test_sink->channel_layouts[i];
        if (soundio_channel_layout_equal(layout, test_layout))
            return true;
    }
    return false;
}

static bool sink_formats_compatible(const struct GrooveSink *example_sink,
        const struct GrooveSink *test_sink)
{
    // buffer_sample_count 0 means we don't care
    if (test_sink->buffer_sample_count != 0 &&
            example_sink->buffer_sample_count != test_sink->buffer_sample_count)
    {
        return false;
    }
    if (example_sink->gain != test_sink->gain)
        return false;

    // test_sink must support everything example_sink supports
    // planar vs interleaved
    bool test_sink_planar_ok = (test_sink->flags & GrooveSinkFlagPlanarOk);
    bool test_sink_interleaved_ok = (test_sink->flags & GrooveSinkFlagPlanarOk);
    if (!test_sink_planar_ok && !test_sink_interleaved_ok) {
        test_sink_planar_ok = true;
        test_sink_interleaved_ok = true;
    }
    bool example_sink_planar_ok = (example_sink->flags & GrooveSinkFlagPlanarOk);
    bool example_sink_interleaved_ok = (example_sink->flags & GrooveSinkFlagPlanarOk);
    if (!example_sink_planar_ok && !example_sink_interleaved_ok) {
        example_sink_planar_ok = true;
        example_sink_interleaved_ok = true;
    }
    if (example_sink_planar_ok && !test_sink_planar_ok)
        return false;
    if (example_sink_interleaved_ok && !test_sink_interleaved_ok)
        return false;

    // sample rate
    if (!example_sink->sample_rates && test_sink->sample_rates)
        return false;
    for (int i = 0; i < example_sink->sample_rate_count; i += 1) {
        if (!sink_supports_sample_rate_range(test_sink, &example_sink->sample_rates[i]))
            return false;
    }

    // sample format
    if (!example_sink->sample_formats && test_sink->sample_formats)
        return false;
    for (int i = 0; i < example_sink->sample_format_count; i += 1) {
        if (!sink_supports_sample_format(test_sink, example_sink->sample_formats[i]))
            return false;
    }

    // channel layout
    if (!example_sink->channel_layouts && test_sink->channel_layouts)
        return false;
    for (int i = 0; i < example_sink->channel_layout_count; i += 1) {
        const struct SoundIoChannelLayout *layout = &example_sink->channel_layouts[i];
        if (!sink_supports_channel_layout(test_sink, layout))
            return false;
    }

    return true;
}

static int remove_sink_from_map(struct GrooveSink *sink) {
    struct GroovePlaylist *playlist = sink->playlist;
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    struct SinkMap *map_item = p->sink_map;
    struct SinkMap *prev_map_item = NULL;
    while (map_item) {
        struct SinkMap *next_map_item = map_item->next;
        struct SinkStack *stack_item = map_item->stack_head;
        struct SinkStack *prev_stack_item = NULL;
        while (stack_item) {
            struct SinkStack *next_stack_item = stack_item->next;
            struct GrooveSink *item_sink = stack_item->sink;
            if (item_sink == sink) {
                DEALLOCATE(stack_item);
                if (prev_stack_item) {
                    prev_stack_item->next = next_stack_item;
                } else if (next_stack_item) {
                    map_item->stack_head = next_stack_item;
                } else {
                    // the stack is empty; delete the map item
                    DEALLOCATE(map_item);
                    p->sink_map_count -= 1;
                    if (prev_map_item) {
                        prev_map_item->next = next_map_item;
                    } else {
                        p->sink_map = next_map_item;
                    }
                }
                return 0;
            }

            prev_stack_item = stack_item;
            stack_item = next_stack_item;
        }
        prev_map_item = map_item;
        map_item = next_map_item;
    }

    return GrooveErrorSinkNotFound;
}

static int add_sink_to_map(struct GroovePlaylist *playlist, struct GrooveSink *sink) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    struct SinkStack *stack_entry = ALLOCATE(struct SinkStack, 1);

    if (!stack_entry)
        return GrooveErrorNoMem;

    stack_entry->sink = sink;

    struct SinkMap *map_item = p->sink_map;
    while (map_item) {
        // if our sink matches the example sink from this map entry,
        // push our sink onto the stack and we're done
        struct GrooveSink *example_sink = map_item->stack_head->sink;
        if (sink_formats_compatible(example_sink, sink)) {
            stack_entry->next = map_item->stack_head->next;
            map_item->stack_head->next = stack_entry;
            return 0;
        }
        // maybe we need to swap the example sink with the new sink to make
        // it work. In this case we need to rebuild the filter graph.
        if (sink_formats_compatible(sink, example_sink)) {
            stack_entry->next = map_item->stack_head;
            map_item->stack_head = stack_entry;
            p->rebuild_filter_graph_flag = 1;
            return 0;
        }
        map_item = map_item->next;
    }
    // we did not find somewhere to put it, so push it onto the stack.
    struct SinkMap *map_entry = ALLOCATE(struct SinkMap, 1);
    map_entry->stack_head = stack_entry;
    if (!map_entry) {
        DEALLOCATE(stack_entry);
        return GrooveErrorNoMem;
    }
    if (p->sink_map) {
        map_entry->next = p->sink_map;
        p->sink_map = map_entry;
    } else {
        p->sink_map = map_entry;
    }
    p->rebuild_filter_graph_flag = 1;
    p->sink_map_count += 1;
    return 0;
}

static int groove_sink_play(struct GrooveSink *sink) {
    if (sink->play)
        sink->play(sink);

    return 0;
}

static int groove_sink_pause(struct GrooveSink *sink) {
    if (sink->pause)
        sink->pause(sink);

    return 0;
}

int groove_sink_detach(struct GrooveSink *sink) {
    struct GroovePlaylist *playlist = sink->playlist;

    assert(playlist);
    if (!playlist)
        return GrooveErrorInvalid;

    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;

    if (s->audioq) {
        groove_queue_abort(s->audioq);
        groove_queue_flush(s->audioq);
    }

    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    pthread_mutex_lock(&p->decode_head_mutex);
    int err = remove_sink_from_map(sink);
    pthread_mutex_unlock(&p->decode_head_mutex);

    sink->playlist = NULL;

    return err;
}

int groove_sink_attach(struct GrooveSink *sink, struct GroovePlaylist *playlist) {
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;

    // cache computed audio format stuff
    s->min_audioq_size = sink->buffer_size_bytes;
    av_log(NULL, AV_LOG_INFO, "audio queue size: %d\n", s->min_audioq_size);

    // add the sink to the entry that matches its audio format
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    // must do this above add_sink_to_map to avid race condition
    sink->playlist = playlist;

    pthread_mutex_lock(&p->decode_head_mutex);
    int err = add_sink_to_map(playlist, sink);
    pthread_mutex_lock(&p->drain_cond_mutex);
    pthread_cond_signal(&p->sink_drain_cond);
    pthread_mutex_unlock(&p->drain_cond_mutex);
    pthread_mutex_unlock(&p->decode_head_mutex);

    if (err < 0) {
        sink->playlist = NULL;
        av_log(NULL, AV_LOG_ERROR, "unable to attach device: out of memory\n");
        return err;
    }

    // in case we've called abort on the queue, reset
    groove_queue_reset(s->audioq);

    return 0;
}

int groove_sink_buffer_get(struct GrooveSink *sink, struct GrooveBuffer **buffer, int block) {
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;

    if (groove_queue_get(s->audioq, (void**)buffer, block) == 1) {
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

int groove_sink_buffer_peek(struct GrooveSink *sink, int block) {
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;
    return groove_queue_peek(s->audioq, block);
}

struct GroovePlaylist * groove_playlist_create(struct Groove *groove) {
    struct GroovePlaylistPrivate *p = ALLOCATE(struct GroovePlaylistPrivate, 1);
    if (!p) {
        av_log(NULL, AV_LOG_ERROR, "unable to allocate playlist\n");
        return NULL;
    }
    struct GroovePlaylist *playlist = &p->externals;

    p->groove = groove;

    // the one that the playlist can read
    playlist->gain = 1.0;
    // the other volume multiplied by the playlist item's gain
    p->volume = 1.0;

    // set this flag to true so that a race condition does not send the end of
    // queue sentinel early.
    p->sent_end_of_q = 1;

    p->detect_full_sinks = any_sink_full;

    if (pthread_mutex_init(&p->decode_head_mutex, NULL) != 0) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate decode head mutex\n");
        return NULL;
    }
    p->decode_head_mutex_inited = 1;

    if (pthread_mutex_init(&p->drain_cond_mutex, NULL) != 0) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate drain cond mutex\n");
        return NULL;
    }
    p->drain_cond_mutex_inited = 1;

    if (pthread_cond_init(&p->decode_head_cond, NULL) != 0) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate decode head mutex condition\n");
        return NULL;
    }
    p->decode_head_cond_inited = 1;

    if (pthread_cond_init(&p->sink_drain_cond, NULL) != 0) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate sink drain mutex condition\n");
        return NULL;
    }
    p->sink_drain_cond_inited = 1;

    p->in_frame = av_frame_alloc();

    if (!p->in_frame) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to allocate frame\n");
        return NULL;
    }

    if (pthread_create(&p->thread_id, NULL, decode_thread, playlist)) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to create playlist thread\n");
        return NULL;
    }
    p->thread_inited = true;

    p->volume_filter = avfilter_get_by_name("volume");
    if (!p->volume_filter) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to get volume filter\n");
        return NULL;
    }

    p->compand_filter = avfilter_get_by_name("compand");
    if (!p->compand_filter) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to get compand filter\n");
        return NULL;
    }

    p->abuffer_filter = avfilter_get_by_name("abuffer");
    if (!p->abuffer_filter) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to get abuffer filter\n");
        return NULL;
    }

    p->asplit_filter = avfilter_get_by_name("asplit");
    if (!p->asplit_filter) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to get asplit filter\n");
        return NULL;
    }

    p->aformat_filter = avfilter_get_by_name("aformat");
    if (!p->aformat_filter) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to get aformat filter\n");
        return NULL;
    }

    p->abuffersink_filter = avfilter_get_by_name("abuffersink");
    if (!p->abuffersink_filter) {
        groove_playlist_destroy(playlist);
        av_log(NULL, AV_LOG_ERROR, "unable to get abuffersink filter\n");
        return NULL;
    }

    return playlist;
}

void groove_playlist_destroy(struct GroovePlaylist *playlist) {
    groove_playlist_clear(playlist);

    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    // wait for decode thread to finish
    if (p->thread_inited) {
        pthread_mutex_lock(&p->decode_head_mutex);
        p->abort_request = true;
        pthread_cond_signal(&p->decode_head_cond);
        pthread_mutex_unlock(&p->decode_head_mutex);
    }

    if (p->drain_cond_mutex_inited) {
        pthread_mutex_lock(&p->drain_cond_mutex);
        pthread_cond_signal(&p->sink_drain_cond);
        pthread_mutex_unlock(&p->drain_cond_mutex);
    }

    pthread_join(p->thread_id, NULL);

    every_sink(playlist, groove_sink_detach, 0);

    avfilter_graph_free(&p->filter_graph);
    av_frame_free(&p->in_frame);

    if (p->decode_head_mutex_inited)
        pthread_mutex_destroy(&p->decode_head_mutex);

    if (p->drain_cond_mutex_inited)
        pthread_mutex_destroy(&p->drain_cond_mutex);

    if (p->decode_head_cond_inited)
        pthread_cond_destroy(&p->decode_head_cond);

    if (p->sink_drain_cond_inited)
        pthread_cond_destroy(&p->sink_drain_cond);

    DEALLOCATE(p);
}

void groove_playlist_play(struct GroovePlaylist *playlist) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    if (!GROOVE_ATOMIC_EXCHANGE(p->paused, false))
        return;
    every_sink(playlist, groove_sink_play, 0);
}

void groove_playlist_pause(struct GroovePlaylist *playlist) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    if (GROOVE_ATOMIC_EXCHANGE(p->paused, true))
        return;
    every_sink(playlist, groove_sink_pause, 0);
}

void groove_playlist_seek(struct GroovePlaylist *playlist, struct GroovePlaylistItem *item, double seconds) {
    struct GrooveFile * file = item->file;
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    int64_t ts = seconds * f->audio_st->time_base.den / f->audio_st->time_base.num;
    if (f->ic->start_time != AV_NOPTS_VALUE)
        ts += f->ic->start_time;

    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    pthread_mutex_lock(&p->decode_head_mutex);
    pthread_mutex_lock(&f->seek_mutex);

    f->seek_pos = ts;
    f->seek_flush = 1;

    pthread_mutex_unlock(&f->seek_mutex);

    p->decode_head = item;
    pthread_cond_signal(&p->decode_head_cond);
    pthread_mutex_unlock(&p->decode_head_mutex);
}

struct GroovePlaylistItem *groove_playlist_insert(struct GroovePlaylist *playlist,
        struct GrooveFile *file, double gain, double peak, struct GroovePlaylistItem *next)
{
    struct GroovePlaylistItem * item = ALLOCATE(struct GroovePlaylistItem, 1);
    if (!item)
        return NULL;

    item->file = file;
    item->next = next;
    item->gain = gain;
    item->peak = peak;

    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    // lock decode_head_mutex so that decode_head cannot point to a new item
    // while we're screwing around with the queue
    pthread_mutex_lock(&p->decode_head_mutex);

    if (next) {
        if (next->prev) {
            item->prev = next->prev;
            item->prev->next = item;
        } else {
            playlist->head = item;
        }
        next->prev = item;
    } else if (!playlist->head) {
        playlist->head = item;
        playlist->tail = item;

        pthread_mutex_lock(&f->seek_mutex);
        f->seek_pos = 0;
        f->seek_flush = 0;
        pthread_mutex_unlock(&f->seek_mutex);

        p->decode_head = playlist->head;
        pthread_cond_signal(&p->decode_head_cond);
    } else {
        item->prev = playlist->tail;
        playlist->tail->next = item;
        playlist->tail = item;
    }

    pthread_mutex_unlock(&p->decode_head_mutex);
    return item;
}

static int purge_sink(struct GrooveSink *sink) {
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;

    groove_queue_purge(s->audioq);

    struct GroovePlaylist *playlist = sink->playlist;
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    struct GroovePlaylistItem *item = p->purge_item;

    if (sink->purge)
        sink->purge(sink, item);

    return 0;
}

void groove_playlist_remove(struct GroovePlaylist *playlist, struct GroovePlaylistItem *item) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    pthread_mutex_lock(&p->decode_head_mutex);

    // if it's currently being played, seek to the next item
    if (item == p->decode_head) {
        p->decode_head = item->next;
    }

    if (item->prev) {
        item->prev->next = item->next;
    } else {
        playlist->head = item->next;
    }
    if (item->next) {
        item->next->prev = item->prev;
    } else {
        playlist->tail = item->prev;
    }

    // in each sink,
    // we must be absolutely sure to purge the audio buffer queue
    // of references to item before freeing it at the bottom of this method
    p->purge_item = item;
    every_sink(playlist, purge_sink, 0);
    p->purge_item = NULL;

    pthread_mutex_lock(&p->drain_cond_mutex);
    pthread_cond_signal(&p->sink_drain_cond);
    pthread_mutex_unlock(&p->drain_cond_mutex);
    pthread_mutex_unlock(&p->decode_head_mutex);

    DEALLOCATE(item);
}

void groove_playlist_clear(struct GroovePlaylist *playlist) {
    struct GroovePlaylistItem * node = playlist->head;
    if (!node) return;
    while (node) {
        struct GroovePlaylistItem *next = node->next;
        groove_playlist_remove(playlist, node);
        node = next;
    }
}

int groove_playlist_count(struct GroovePlaylist *playlist) {
    struct GroovePlaylistItem * node = playlist->head;
    int count = 0;
    while (node) {
        count += 1;
        node = node->next;
    }
    return count;
}

void groove_playlist_set_item_gain_peak(struct GroovePlaylist *playlist, struct GroovePlaylistItem *item,
        double gain, double peak)
{
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    pthread_mutex_lock(&p->decode_head_mutex);
    item->gain = gain;
    item->peak = peak;
    if (item == p->decode_head) {
        update_playlist_volume(playlist);
    }
    pthread_mutex_unlock(&p->decode_head_mutex);
}

void groove_playlist_position(struct GroovePlaylist *playlist, struct GroovePlaylistItem **item,
        double *seconds)
{
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    pthread_mutex_lock(&p->decode_head_mutex);
    if (item)
        *item = p->decode_head;

    if (seconds) {
        if (p->decode_head) {
            struct GrooveFile *file = p->decode_head->file;
            struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;
            *seconds = f->audio_clock;
        } else {
            *seconds = -1.0;
        }
    }
    pthread_mutex_unlock(&p->decode_head_mutex);
}

void groove_playlist_set_gain(struct GroovePlaylist *playlist, double gain) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    pthread_mutex_lock(&p->decode_head_mutex);
    playlist->gain = gain;
    if (p->decode_head)
        update_playlist_volume(playlist);
    pthread_mutex_unlock(&p->decode_head_mutex);
}

int groove_playlist_playing(struct GroovePlaylist *playlist) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;
    return !GROOVE_ATOMIC_LOAD(p->paused);
}

struct GrooveSink * groove_sink_create(struct Groove *groove) {
    struct GrooveSinkPrivate *s = ALLOCATE(struct GrooveSinkPrivate, 1);

    if (!s) {
        av_log(NULL, AV_LOG_ERROR, "could not create sink: out of memory\n");
        return NULL;
    }

    s->groove = groove;

    GROOVE_ATOMIC_STORE(s->audioq_size, 0);
    GROOVE_ATOMIC_STORE(s->audioq_contains_end, false);

    struct GrooveSink *sink = &s->externals;

    sink->buffer_size_bytes = 64 * 1024;
    sink->gain = 1.0;

    s->audioq = groove_queue_create();

    if (!s->audioq) {
        groove_sink_destroy(sink);
        av_log(NULL, AV_LOG_ERROR, "could not create audio buffer: out of memory\n");
        return NULL;
    }

    s->audioq->context = sink;
    s->audioq->cleanup = audioq_cleanup;
    s->audioq->put = audioq_put;
    s->audioq->get = audioq_get;
    s->audioq->purge = audioq_purge;

    return sink;
}

void groove_sink_destroy(struct GrooveSink *sink) {
    if (!sink)
        return;

    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;

    if (s->audioq)
        groove_queue_destroy(s->audioq);

    DEALLOCATE(s);
}

int groove_sink_set_gain(struct GrooveSink *sink, double gain) {
    // we must re-create the sink mapping and the filter graph
    // if the gain changes

    struct GroovePlaylist *playlist = sink->playlist;
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;


    pthread_mutex_lock(&p->decode_head_mutex);
    sink->gain = gain;
    int err = remove_sink_from_map(sink);
    if (err) {
        pthread_mutex_unlock(&p->decode_head_mutex);
        return err;
    }
    err = add_sink_to_map(playlist, sink);
    if (err) {
        pthread_mutex_unlock(&p->decode_head_mutex);
        return err;
    }
    p->rebuild_filter_graph_flag = 1;
    pthread_mutex_unlock(&p->decode_head_mutex);
    return 0;
}

int groove_sink_get_fill_level(struct GrooveSink *sink) {
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;
    return GROOVE_ATOMIC_LOAD(s->audioq_size);
}

void groove_sink_set_buffer_size_bytes(struct GrooveSink *sink, int buffer_size_bytes) {
    struct GroovePlaylist *playlist = (struct GroovePlaylist *) sink->playlist;
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    pthread_mutex_lock(&p->decode_head_mutex);
    sink->buffer_size_bytes = buffer_size_bytes;
    s->min_audioq_size = sink->buffer_size_bytes;
    if (GROOVE_ATOMIC_LOAD(s->audioq_size) < s->min_audioq_size) {
        pthread_mutex_lock(&p->drain_cond_mutex);
        pthread_cond_signal(&p->sink_drain_cond);
        pthread_mutex_unlock(&p->drain_cond_mutex);
    }
    pthread_mutex_unlock(&p->decode_head_mutex);
}

int groove_sink_contains_end_of_playlist(struct GrooveSink *sink) {
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;
    return GROOVE_ATOMIC_LOAD(s->audioq_contains_end);
}

void groove_sink_set_only_format(struct GrooveSink *sink,
        const struct GrooveAudioFormat *audio_format)
{
    struct GrooveSinkPrivate *s = (struct GrooveSinkPrivate *) sink;

    s->prealloc_sample_rate_range.min = audio_format->sample_rate;
    s->prealloc_sample_rate_range.max = audio_format->sample_rate;
    sink->sample_rates = &s->prealloc_sample_rate_range;
    sink->sample_rate_count = 1;
    sink->sample_rate_default = audio_format->sample_rate;

    sink->channel_layout_default = audio_format->layout;
    sink->channel_layouts = &sink->channel_layout_default;
    sink->channel_layout_count = 1;

    sink->sample_format_default = audio_format->format;
    sink->sample_formats = &sink->sample_format_default;
    sink->sample_format_count = 1;

    sink->flags = (audio_format->is_planar ? GrooveSinkFlagPlanarOk : GrooveSinkFlagInterleavedOk);
}

void groove_playlist_set_fill_mode(struct GroovePlaylist *playlist, enum GrooveFillMode mode) {
    struct GroovePlaylistPrivate *p = (struct GroovePlaylistPrivate *) playlist;

    pthread_mutex_lock(&p->decode_head_mutex);

    if (mode == GrooveFillModeEverySinkFull) {
        p->detect_full_sinks = every_sink_full;
    } else {
        p->detect_full_sinks = any_sink_full;
    }

    pthread_mutex_unlock(&p->decode_head_mutex);
}

