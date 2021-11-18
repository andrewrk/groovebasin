/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_ENCODER_H
#define GROOVE_ENCODER_H

#include <groove/groove.h>

/// attach a GrooveEncoder to a playlist to keep a buffer of encoded audio full.
/// for example you could use it to implement an http audio stream
struct GrooveEncoder {
    /// The desired audio format to encode.
    /// ::groove_encoder_create defaults these to 44100 Hz,
    /// signed 16-bit int, stereo.
    /// These are preferences; if a setting cannot be used, a substitute will be
    /// used instead. actual_audio_format is set to the actual values.
    struct GrooveAudioFormat target_audio_format;

    /// Select encoding quality by choosing a target bit rate in bits per
    /// second. Note that typically you see this expressed in "kbps", such
    /// as 320kbps or 128kbps. Surprisingly, in this circumstance 1 kbps is
    /// 1000 bps, *not* 1024 bps as you would expect.
    /// ::groove_encoder_create defaults this to 256000
    int bit_rate;

    /// optional - choose a short name for the format
    /// to help libgroove guess which format to use
    /// use `avconv -formats` to get a list of possibilities
    const char *format_short_name;
    /// optional - choose a short name for the codec
    /// to help libgroove guess which codec to use
    /// use `avconv -codecs` to get a list of possibilities
    const char *codec_short_name;
    /// optional - provide an example filename
    /// to help libgroove guess which format/codec to use
    const char *filename;
    /// optional - provide a mime type string
    /// to help libgroove guess which format/codec to use
    const char *mime_type;

    /// how big the sink buffer should be
    /// ::groove_encoder_create defaults this to 64KB
    int sink_buffer_size_bytes;

    /// how big the encoded audio buffer should be, in bytes
    /// ::groove_encoder_create defaults this to 16384
    int encoded_buffer_size;

    /// This volume adjustment to make to this player.
    /// It is recommended that you leave this at 1.0 and instead adjust the
    /// gain of the underlying playlist.
    /// If you want to change this value after you have already attached the
    /// sink to the playlist, you must use ::groove_encoder_set_gain.
    /// float format. Defaults to 1.0
    double gain;

    /// read-only. set when attached and cleared when detached
    struct GroovePlaylist *playlist;

    /// read-only. set to the actual format you get when you attach to a
    /// playlist. ideally will be the same as target_audio_format but might
    /// not be.
    struct GrooveAudioFormat actual_audio_format;
};

GROOVE_EXPORT struct GrooveEncoder *groove_encoder_create(struct Groove *);
/// detach before destroying
GROOVE_EXPORT void groove_encoder_destroy(struct GrooveEncoder *encoder);

/// once you attach, you must detach before destroying the playlist
/// at playlist begin, format headers are generated. when end of playlist is
/// reached, format trailers are generated.
GROOVE_EXPORT int groove_encoder_attach(struct GrooveEncoder *encoder,
        struct GroovePlaylist *playlist);
GROOVE_EXPORT int groove_encoder_detach(struct GrooveEncoder *encoder);

/// returns < 0 on error, #GROOVE_BUFFER_NO on aborted (block=1) or no buffer
/// ready (block=0), #GROOVE_BUFFER_YES on buffer returned, and GROOVE_BUFFER_END
/// on end of playlist.
/// buffer is always set to either a valid GrooveBuffer or `NULL`.
GROOVE_EXPORT int groove_encoder_buffer_get(struct GrooveEncoder *encoder,
        struct GrooveBuffer **buffer, int block);

/// returns < 0 on error, 0 on no buffer ready, 1 on buffer ready
/// if block is 1, block until buffer is ready
GROOVE_EXPORT int groove_encoder_buffer_peek(struct GrooveEncoder *encoder, int block);

/// see docs for groove_file_metadata_get
GROOVE_EXPORT struct GrooveTag *groove_encoder_metadata_get(struct GrooveEncoder *encoder,
        const char *key, const struct GrooveTag *prev, int flags);

/// see docs for ::groove_file_metadata_set
GROOVE_EXPORT int groove_encoder_metadata_set(struct GrooveEncoder *encoder, const char *key,
        const char *value, int flags);

/// get the position of the encode head
/// both the current playlist item and the position in seconds in the playlist
/// item are given. item will be set to NULL if the playlist is empty
/// you may pass NULL for item or seconds
GROOVE_EXPORT void groove_encoder_position(struct GrooveEncoder *encoder,
        struct GroovePlaylistItem **item, double *seconds);

/// See the gain property of GrooveSink. It is recommended that you leave this
/// at 1.0 and instead adjust the gain of the playlist.
/// returns 0 on success, < 0 on error
GROOVE_EXPORT int groove_encoder_set_gain(struct GrooveEncoder *encoder, double gain);

#endif
