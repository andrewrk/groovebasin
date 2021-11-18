/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "groove_internal.h"
#include "util.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include <libavutil/channel_layout.h>

void groove_panic(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    abort();
}

enum SoundIoChannelId from_ffmpeg_channel_id(uint64_t ffmpeg_channel_id) {
    switch (ffmpeg_channel_id) {
        default:                          return SoundIoChannelIdInvalid;
        case AV_CH_FRONT_LEFT:            return SoundIoChannelIdFrontLeft;
        case AV_CH_FRONT_RIGHT:           return SoundIoChannelIdFrontRight;
        case AV_CH_FRONT_CENTER:          return SoundIoChannelIdFrontCenter;
        case AV_CH_LOW_FREQUENCY:         return SoundIoChannelIdLfe;
        case AV_CH_BACK_LEFT:             return SoundIoChannelIdBackLeft;
        case AV_CH_BACK_RIGHT:            return SoundIoChannelIdBackRight;
        case AV_CH_FRONT_LEFT_OF_CENTER:  return SoundIoChannelIdFrontLeftCenter;
        case AV_CH_FRONT_RIGHT_OF_CENTER: return SoundIoChannelIdFrontRightCenter;
        case AV_CH_BACK_CENTER:           return SoundIoChannelIdBackCenter;
        case AV_CH_SIDE_LEFT:             return SoundIoChannelIdSideLeft;
        case AV_CH_SIDE_RIGHT:            return SoundIoChannelIdSideRight;
        case AV_CH_TOP_CENTER:            return SoundIoChannelIdTopCenter;
        case AV_CH_TOP_FRONT_LEFT:        return SoundIoChannelIdTopFrontLeft;
        case AV_CH_TOP_FRONT_CENTER:      return SoundIoChannelIdTopFrontCenter;
        case AV_CH_TOP_FRONT_RIGHT:       return SoundIoChannelIdTopFrontRight;
        case AV_CH_TOP_BACK_LEFT:         return SoundIoChannelIdTopBackLeft;
        case AV_CH_TOP_BACK_CENTER:       return SoundIoChannelIdTopBackCenter;
        case AV_CH_TOP_BACK_RIGHT:        return SoundIoChannelIdTopBackRight;
    }
}

void from_ffmpeg_layout(uint64_t in_layout, struct SoundIoChannelLayout *out_layout) {
    int channel_count = av_get_channel_layout_nb_channels(in_layout);
    channel_count = groove_min_int(channel_count, SOUNDIO_MAX_CHANNELS);

    out_layout->channel_count = channel_count;
    for (int i = 0; i < channel_count; i += 1) {
        uint64_t ffmpeg_channel_id = av_channel_layout_extract_channel(in_layout, i);
        enum SoundIoChannelId channel_id = from_ffmpeg_channel_id(ffmpeg_channel_id);
        out_layout->channels[i] = channel_id;
    }
    soundio_channel_layout_detect_builtin(out_layout);
}

enum SoundIoFormat from_ffmpeg_format(enum AVSampleFormat fmt) {
    switch (fmt) {
        default:
            return SoundIoFormatInvalid;
        case AV_SAMPLE_FMT_U8:
        case AV_SAMPLE_FMT_U8P:
            return SoundIoFormatU8;
        case AV_SAMPLE_FMT_S16:
        case AV_SAMPLE_FMT_S16P:
            return SoundIoFormatS16NE;
        case AV_SAMPLE_FMT_S32:
        case AV_SAMPLE_FMT_S32P:
            return SoundIoFormatS32NE;
        case AV_SAMPLE_FMT_FLT:
        case AV_SAMPLE_FMT_FLTP:
            return SoundIoFormatFloat32NE;
        case AV_SAMPLE_FMT_DBL:
        case AV_SAMPLE_FMT_DBLP:
            return SoundIoFormatFloat64NE;
    }
}

bool from_ffmpeg_format_planar(enum AVSampleFormat fmt) {
    switch (fmt) {
        default:
            return false;
        case AV_SAMPLE_FMT_U8P:
        case AV_SAMPLE_FMT_S16P:
        case AV_SAMPLE_FMT_S32P:
        case AV_SAMPLE_FMT_FLTP:
        case AV_SAMPLE_FMT_DBLP:
            return true;
    }
}

uint64_t to_ffmpeg_channel_id(enum SoundIoChannelId channel_id) {
    switch (channel_id) {
        default: return 0;
        case SoundIoChannelIdInvalid: return 0;
        case SoundIoChannelIdFrontLeft: return AV_CH_FRONT_LEFT;
        case SoundIoChannelIdFrontRight: return AV_CH_FRONT_RIGHT;
        case SoundIoChannelIdFrontCenter: return AV_CH_FRONT_CENTER;
        case SoundIoChannelIdLfe: return AV_CH_LOW_FREQUENCY;
        case SoundIoChannelIdBackLeft: return AV_CH_BACK_LEFT;
        case SoundIoChannelIdBackRight: return AV_CH_BACK_RIGHT;
        case SoundIoChannelIdFrontLeftCenter: return AV_CH_FRONT_LEFT_OF_CENTER;
        case SoundIoChannelIdFrontRightCenter: return AV_CH_FRONT_RIGHT_OF_CENTER;
        case SoundIoChannelIdBackCenter: return AV_CH_BACK_CENTER;
        case SoundIoChannelIdSideLeft: return AV_CH_SIDE_LEFT;
        case SoundIoChannelIdSideRight: return AV_CH_SIDE_RIGHT;
        case SoundIoChannelIdTopCenter: return AV_CH_TOP_CENTER;
        case SoundIoChannelIdTopFrontLeft: return AV_CH_TOP_FRONT_LEFT;
        case SoundIoChannelIdTopFrontCenter: return AV_CH_TOP_FRONT_CENTER;
        case SoundIoChannelIdTopFrontRight: return AV_CH_TOP_FRONT_RIGHT;
        case SoundIoChannelIdTopBackLeft: return AV_CH_TOP_BACK_LEFT;
        case SoundIoChannelIdTopBackCenter: return AV_CH_TOP_BACK_CENTER;
        case SoundIoChannelIdTopBackRight: return AV_CH_TOP_BACK_RIGHT;
    }
}

uint64_t to_ffmpeg_channel_layout(const struct SoundIoChannelLayout *channel_layout) {
    uint64_t result = 0;
    for (int i = 0; i < channel_layout->channel_count; i += 1) {
        enum SoundIoChannelId channel_id = channel_layout->channels[i];
        result |= to_ffmpeg_channel_id(channel_id);
    }
    return result;
}

enum AVSampleFormat to_ffmpeg_fmt_params(enum SoundIoFormat format, bool is_planar) {
    switch (format) {
        default:
            return AV_SAMPLE_FMT_NONE;
        case SoundIoFormatU8:
            return is_planar ? AV_SAMPLE_FMT_U8P : AV_SAMPLE_FMT_U8;
        case SoundIoFormatS16NE:
            return is_planar ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S16;
        case SoundIoFormatS32NE:
            return is_planar ? AV_SAMPLE_FMT_S32P : AV_SAMPLE_FMT_S32;
        case SoundIoFormatFloat32NE:
            return is_planar ? AV_SAMPLE_FMT_FLTP : AV_SAMPLE_FMT_FLT;
        case SoundIoFormatFloat64NE:
            return is_planar ? AV_SAMPLE_FMT_DBLP : AV_SAMPLE_FMT_DBL;
    }
}

enum AVSampleFormat to_ffmpeg_fmt(const struct GrooveAudioFormat *fmt) {
    return to_ffmpeg_fmt_params(fmt->format, fmt->is_planar);
}
