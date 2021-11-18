/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_UTIL_H
#define GROOVE_UTIL_H

#include "groove_internal.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>

#define BREAKPOINT __asm("int $0x03")

#define ALLOCATE_NONZERO(Type, count) av_malloc((count) * sizeof(Type))

#define ALLOCATE(Type, count) av_mallocz(count * sizeof(Type))

#define REALLOCATE_NONZERO(Type, old, new_count) av_realloc(old, (new_count) * sizeof(Type))

#define DEALLOCATE(ptr) av_free(ptr)

#define ARRAY_LENGTH(array) (sizeof(array)/sizeof((array)[0]))

void groove_panic(const char *format, ...)
    __attribute__((cold))
    __attribute__ ((noreturn))
    __attribute__ ((format (printf, 1, 2)));


static inline int groove_min_int(int a, int b) {
    return (a <= b) ? a : b;
}

static inline int groove_max_int(int a, int b) {
    return (a >= b) ? a : b;
}

static inline float groove_max_float(float a, float b) {
    return (a >= b) ? a : b;
}

static inline long groove_max_long(long a, long b) {
    return (a >= b) ? a : b;
}

static inline double groove_max_double(double a, double b) {
    return (a >= b) ? a : b;
}


enum SoundIoChannelId from_ffmpeg_channel_id(uint64_t ffmpeg_channel_id);
void from_ffmpeg_layout(uint64_t in_layout, struct SoundIoChannelLayout *out_layout);
enum SoundIoFormat from_ffmpeg_format(enum AVSampleFormat fmt);
bool from_ffmpeg_format_planar(enum AVSampleFormat fmt);

uint64_t to_ffmpeg_channel_id(enum SoundIoChannelId channel_id);
uint64_t to_ffmpeg_channel_layout(const struct SoundIoChannelLayout *channel_layout);
enum AVSampleFormat to_ffmpeg_fmt(const struct GrooveAudioFormat *fmt);
enum AVSampleFormat to_ffmpeg_fmt_params(enum SoundIoFormat format, bool is_planar);

#endif
