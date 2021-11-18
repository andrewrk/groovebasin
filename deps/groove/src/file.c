/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "file.h"
#include "util.h"
#include "groove_private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static int decode_interrupt_cb(void *ctx) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *)ctx;
    return f ? GROOVE_ATOMIC_LOAD(f->abort_request) : 0;
}

static int avio_read_packet_callback(void *opaque, uint8_t *buf, int buf_size) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *)opaque;
    return f->custom_io->read_packet(f->custom_io, buf, buf_size);
}

static int avio_write_packet_callback(void *opaque, uint8_t *buf, int buf_size) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *)opaque;
    return f->custom_io->write_packet(f->custom_io, buf, buf_size);
}

static int64_t avio_seek_callback(void *opaque, int64_t offset, int whence) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *)opaque;
    return f->custom_io->seek(f->custom_io, offset, whence);
}

static int file_read_packet(struct GrooveCustomIo *custom_io, uint8_t *buf, int buf_size) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *)custom_io->userdata;
    return fread(buf, 1, buf_size, f->stdfile);
}

static int file_write_packet(struct GrooveCustomIo *custom_io, uint8_t *buf, int buf_size) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *)custom_io->userdata;
    return fwrite(buf, 1, buf_size, f->stdfile);
}

static int64_t file_seek(struct GrooveCustomIo *custom_io, int64_t offset, int whence) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *)custom_io->userdata;

    if (whence & GROOVE_SEEK_FORCE) {
        // doesn't matter
        whence -= GROOVE_SEEK_FORCE;
    }

    if (whence & GROOVE_SEEK_SIZE) {
        int err;
        struct stat st;
        if ((err = fstat(fileno(f->stdfile), &st))) {
            return err;
        }
        return st.st_size;
    }

    switch (whence) {
        case SEEK_SET:
        case SEEK_CUR:
        case SEEK_END:
            return fseek(f->stdfile, offset, whence);
    }
    return -1;
}

static void init_file_state(struct GrooveFilePrivate *f) {
    struct Groove *groove = f->groove;
    memset(f, 0, sizeof(struct GrooveFilePrivate));
    f->groove = groove;
    f->audio_stream_index = -1;
    f->seek_pos = -1;
    GROOVE_ATOMIC_STORE(f->abort_request, false);
}

struct GrooveFile *groove_file_create(struct Groove *groove) {
    struct GrooveFilePrivate *f = ALLOCATE_NONZERO(struct GrooveFilePrivate, 1);
    if (!f)
        return NULL;

    init_file_state(f);

    return &f->externals;
}

int groove_file_open_custom(struct GrooveFile *file, struct GrooveCustomIo *custom_io,
        const char *filename_hint)
{
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    f->custom_io = custom_io;

    if (pthread_mutex_init(&f->seek_mutex, NULL)) {
        groove_file_close(file);
        return GrooveErrorSystemResources;
    }

    f->ic = avformat_alloc_context();
    if (!f->ic) {
        groove_file_close(file);
        return GrooveErrorNoMem;
    }
    file->filename = f->ic->filename;
    f->ic->interrupt_callback.callback = decode_interrupt_cb;
    f->ic->interrupt_callback.opaque = f;

    const int buffer_size = 8 * 1024;
    f->avio_buf = ALLOCATE_NONZERO(unsigned char, buffer_size);
    if (!f->avio_buf) {
        groove_file_close(file);
        return GrooveErrorNoMem;
    }

    f->avio = avio_alloc_context(f->avio_buf, buffer_size, 0, f,
            avio_read_packet_callback, avio_write_packet_callback, avio_seek_callback);
    if (!f->avio) {
        groove_file_close(file);
        return GrooveErrorNoMem;
    }
    f->avio->seekable = AVIO_SEEKABLE_NORMAL;
    f->avio->direct = AVIO_FLAG_DIRECT;

    f->ic->pb = f->avio;
    int err = avformat_open_input(&f->ic, filename_hint, NULL, NULL);
    if (err < 0) {
        assert(err != AVERROR(EINVAL));
        groove_file_close(file);
        if (err == AVERROR(ENOMEM)) {
            return GrooveErrorNoMem;
        } else if (err == AVERROR(ENOENT)) {
            return GrooveErrorFileNotFound;
        } else if (err == AVERROR(EPERM)) {
            return GrooveErrorPermissions;
        } else {
            return GrooveErrorUnknownFormat;
        }
    }

    err = avformat_find_stream_info(f->ic, NULL);
    if (err < 0) {
        groove_file_close(file);
        return GrooveErrorStreamNotFound;
    }

    // set all streams to discard. in a few lines here we will find the audio
    // stream and cancel discarding it
    if (f->ic->nb_streams > INT_MAX) {
        groove_file_close(file);
        return GrooveErrorTooManyStreams;
    }
    int stream_count = (int)f->ic->nb_streams;

    for (int i = 0; i < stream_count; i++)
        f->ic->streams[i]->discard = AVDISCARD_ALL;

    f->audio_stream_index = av_find_best_stream(f->ic, AVMEDIA_TYPE_AUDIO, -1, -1, &f->decoder, 0);

    if (f->audio_stream_index < 0) {
        groove_file_close(file);
        return GrooveErrorStreamNotFound;
    }

    if (!f->decoder) {
        groove_file_close(file);
        return GrooveErrorDecoderNotFound;
    }

    f->audio_st = f->ic->streams[f->audio_stream_index];
    f->audio_st->discard = AVDISCARD_DEFAULT;

    AVCodecContext *avctx = f->audio_st->codec;

    if (avcodec_open2(avctx, f->decoder, NULL) < 0) {
        groove_file_close(file);
        return GrooveErrorDecoding;
    }

    if (!avctx->channel_layout)
        avctx->channel_layout = av_get_default_channel_layout(avctx->channels);
    if (!avctx->channel_layout) {
        groove_file_close(file);
        return GrooveErrorInvalidChannelLayout;
    }

    // copy the audio stream metadata to the context metadata
    av_dict_copy(&f->ic->metadata, f->audio_st->metadata, 0);

    return 0;
}

int groove_file_open(struct GrooveFile *file,
        const char *filename, const char *filename_hint)
{
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    f->stdfile = fopen(filename, "rb");
    if (!f->stdfile) {
        int err = errno;
        assert(err != EINVAL);
        groove_file_close(file);
        if (err == ENOMEM) {
            return GrooveErrorNoMem;
        } else if (err == ENOENT) {
            return GrooveErrorFileNotFound;
        } else if (err == EPERM) {
            return GrooveErrorPermissions;
        } else {
            return GrooveErrorFileSystem;
        }
    }

    f->prealloc_custom_io.userdata = f;
    f->prealloc_custom_io.read_packet = file_read_packet;
    f->prealloc_custom_io.write_packet = file_write_packet;
    f->prealloc_custom_io.seek = file_seek;

    return groove_file_open_custom(file, &f->prealloc_custom_io, filename_hint);
}

// should be safe to call no matter what state the file is in
void groove_file_close(struct GrooveFile *file) {
    if (!file)
        return;

    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *)file;

    GROOVE_ATOMIC_STORE(f->abort_request, true);

    if (f->audio_stream_index >= 0) {
        AVCodecContext *avctx = f->ic->streams[f->audio_stream_index]->codec;

        av_packet_unref(&f->audio_pkt);

        f->ic->streams[f->audio_stream_index]->discard = AVDISCARD_ALL;
        avcodec_close(avctx);
        f->audio_st = NULL;
        f->audio_stream_index = -1;
    }

    // disable interrupting
    GROOVE_ATOMIC_STORE(f->abort_request, false);

    if (f->ic)
        avformat_close_input(&f->ic);

    pthread_mutex_destroy(&f->seek_mutex);

    if (f->avio)
        av_free(f->avio);

    if (f->stdfile)
        fclose(f->stdfile);

    init_file_state(f);
}

void groove_file_destroy(struct GrooveFile *file) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *)file;

    if (!file)
        return;

    groove_file_close(file);

    DEALLOCATE(f);
}


const char *groove_file_short_names(struct GrooveFile *file) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;
    return f->ic->iformat->name;
}

double groove_file_duration(struct GrooveFile *file) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;
    double time_base = av_q2d(f->audio_st->time_base);
    return time_base * f->audio_st->duration;
}

void groove_file_audio_format(struct GrooveFile *file, struct GrooveAudioFormat *audio_format) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    AVCodecContext *codec_ctx = f->audio_st->codec;
    audio_format->sample_rate = codec_ctx->sample_rate;
    from_ffmpeg_layout(codec_ctx->channel_layout, &audio_format->layout);
    audio_format->format = from_ffmpeg_format(codec_ctx->sample_fmt);
    audio_format->is_planar = from_ffmpeg_format_planar(codec_ctx->sample_fmt);
}

struct GrooveTag *groove_file_metadata_get(struct GrooveFile *file, const char *key,
        const struct GrooveTag *prev, int flags)
{
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;
    const AVDictionaryEntry *e = (const AVDictionaryEntry *) prev;
    if (key && key[0] == 0)
        flags |= AV_DICT_IGNORE_SUFFIX;
    return (struct GrooveTag *) av_dict_get(f->ic->metadata, key, e, flags);
}

int groove_file_metadata_set(struct GrooveFile *file, const char *key,
        const char *value, int flags)
{
    file->dirty = 1;
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;
    return av_dict_set(&f->ic->metadata, key, value, flags);
}

const char *groove_tag_key(struct GrooveTag *tag) {
    AVDictionaryEntry *e = (AVDictionaryEntry *) tag;
    return e->key;
}

const char *groove_tag_value(struct GrooveTag *tag) {
    AVDictionaryEntry *e = (AVDictionaryEntry *) tag;
    return e->value;
}

static void cleanup_save(struct GrooveFile *file) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    av_packet_unref(&f->audio_pkt);
    if (f->tempfile_exists) {
        remove(f->oc->filename);
        f->tempfile_exists = 0;
    }
    if (f->oc) {
        avio_closep(&f->oc->pb);
        avformat_free_context(f->oc);
        f->oc = NULL;
    }
}

int groove_file_save_as(struct GrooveFile *file, const char *filename) {
    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    // detect output format
    AVOutputFormat *ofmt = av_guess_format(f->ic->iformat->name, f->ic->filename, NULL);
    if (!ofmt) {
        return GrooveErrorUnknownFormat;
    }

    // allocate output media context
    f->oc = avformat_alloc_context();
    if (!f->oc) {
        cleanup_save(file);
        return GrooveErrorNoMem;
    }

    f->oc->oformat = ofmt;
    snprintf(f->oc->filename, sizeof(f->oc->filename), "%s", filename);

    // open output file if needed
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&f->oc->pb, f->oc->filename, AVIO_FLAG_WRITE) < 0) {
            cleanup_save(file);
            return GrooveErrorFileSystem;
        }
        f->tempfile_exists = 1;
    }

    if (f->ic->nb_streams > INT_MAX) {
        cleanup_save(file);
        return GrooveErrorTooManyStreams;
    }
    int stream_count = (int)f->ic->nb_streams;

    // add all the streams
    for (int i = 0; i < stream_count; i++) {
        AVStream *in_stream = f->ic->streams[i];
        AVStream *out_stream = avformat_new_stream(f->oc, NULL);
        if (!out_stream) {
            cleanup_save(file);
            return GrooveErrorNoMem;
        }
        out_stream->id = in_stream->id;
        out_stream->disposition = in_stream->disposition;
        out_stream->time_base = in_stream->time_base;

        AVCodecContext *icodec = in_stream->codec;
        AVCodecContext *ocodec = out_stream->codec;
        ocodec->bits_per_raw_sample    = icodec->bits_per_raw_sample;
        ocodec->chroma_sample_location = icodec->chroma_sample_location;
        ocodec->codec_id   = icodec->codec_id;
        ocodec->codec_type = icodec->codec_type;
        if (!ocodec->codec_tag) {
            if (!f->oc->oformat->codec_tag ||
                 av_codec_get_id (f->oc->oformat->codec_tag, icodec->codec_tag) == ocodec->codec_id ||
                 av_codec_get_tag(f->oc->oformat->codec_tag, icodec->codec_id) <= 0)
                ocodec->codec_tag = icodec->codec_tag;
        }
        ocodec->bit_rate       = icodec->bit_rate;
        ocodec->rc_max_rate    = icodec->rc_max_rate;
        ocodec->rc_buffer_size = icodec->rc_buffer_size;
        ocodec->field_order    = icodec->field_order;

        uint64_t extra_size = (uint64_t)icodec->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE;
        if (extra_size > INT_MAX) {
            cleanup_save(file);
            return GrooveErrorEncoding;
        }
        ocodec->extradata      = ALLOCATE(uint8_t, extra_size);
        if (!ocodec->extradata) {
            cleanup_save(file);
            return GrooveErrorNoMem;
        }
        memcpy(ocodec->extradata, icodec->extradata, icodec->extradata_size);
        ocodec->extradata_size = icodec->extradata_size;
        switch (ocodec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            ocodec->channel_layout     = icodec->channel_layout;
            ocodec->sample_rate        = icodec->sample_rate;
            ocodec->channels           = icodec->channels;
            ocodec->frame_size         = icodec->frame_size;
            ocodec->audio_service_type = icodec->audio_service_type;
            ocodec->block_align        = icodec->block_align;
            break;
        case AVMEDIA_TYPE_VIDEO:
            ocodec->pix_fmt            = icodec->pix_fmt;
            ocodec->width              = icodec->width;
            ocodec->height             = icodec->height;
            ocodec->has_b_frames       = icodec->has_b_frames;
            if (!ocodec->sample_aspect_ratio.num) {
                if (in_stream->sample_aspect_ratio.num) {
                    ocodec->sample_aspect_ratio = in_stream->sample_aspect_ratio;
                } else if (icodec->sample_aspect_ratio.num) {
                    ocodec->sample_aspect_ratio = icodec->sample_aspect_ratio;
                } else {
                    ocodec->sample_aspect_ratio.num = 0;
                    ocodec->sample_aspect_ratio.den = 1;
                }
            }
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            ocodec->width  = icodec->width;
            ocodec->height = icodec->height;
            break;
        case AVMEDIA_TYPE_DATA:
        case AVMEDIA_TYPE_ATTACHMENT:
            break;
        default:
            cleanup_save(file);
            return GrooveErrorEncoding;
        }
    }

    // set metadata
    av_dict_copy(&f->oc->metadata, f->ic->metadata, 0);

    if (avformat_write_header(f->oc, NULL) < 0) {
        cleanup_save(file);
        return GrooveErrorEncoding;
    }

    AVPacket *pkt = &f->audio_pkt;
    for (;;) {
        int err = av_read_frame(f->ic, pkt);
        if (err == AVERROR_EOF) {
            break;
        } else if (err < 0) {
            cleanup_save(file);
            return GrooveErrorDecoding;
        }
        if (av_write_frame(f->oc, pkt) < 0) {
            cleanup_save(file);
            return GrooveErrorEncoding;
        }
        av_packet_unref(pkt);
    }

    if (av_write_trailer(f->oc) < 0) {
        cleanup_save(file);
        return GrooveErrorEncoding;
    }

    f->tempfile_exists = 0;
    cleanup_save(file);

    return 0;
}

int groove_file_save(struct GrooveFile *file) {
    if (!file->dirty)
        return GrooveErrorNoChanges;

    struct GrooveFilePrivate *f = (struct GrooveFilePrivate *) file;

    int temp_filename_len;
    char *temp_filename = groove_create_rand_name(f->groove,
            &temp_filename_len, f->ic->filename, strlen(f->ic->filename));

    if (!temp_filename) {
        cleanup_save(file);
        return GrooveErrorNoMem;
    }

    int err;
    if ((err = groove_file_save_as(file, temp_filename))) {
        cleanup_save(file);
        return err;
    }

    if (rename(temp_filename, f->ic->filename)) {
        f->tempfile_exists = 1;
        cleanup_save(file);
        return GrooveErrorFileSystem;
    }

    file->dirty = 0;
    return 0;
}
