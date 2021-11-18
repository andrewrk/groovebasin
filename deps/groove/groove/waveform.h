/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_WAVEFORM_H
#define GROOVE_WAVEFORM_H

#include <groove/groove.h>

enum GrooveWaveformFormat {
    /// Each frame is an unsigned byte
    GrooveWaveformFormatU8,
};

/// The size of this struct is not part of the public API or ABI.
struct GrooveWaveformInfo {
    /// the playlist item that this info applies to. If item is NULL that means
    /// the end of playlist.
    struct GroovePlaylistItem *item;

    /// use ::groove_waveform_info_ref and ::groove_waveform_info_unref
    int ref_count;

    /// sample rate used to calculate this information
    int sample_rate;
    /// This is the correct duration value for the track, known only after
    /// waveform calculation is complete.
    long actual_frame_count;
    /// This is the duration that was used to create the waveform data. If
    /// this is different than `actual_frame_count` the data is invalid and
    /// must be re-calculated using GrooveFile::override_duration.
    long expected_frame_count;

    int data_size;
    char data[1];
};

struct GrooveWaveform {
    /// read-only. set when attached and cleared when detached
    struct GroovePlaylist *playlist;

    /// How many frames wide the waveform data will be. Defaults to 1920.
    ///
    /// If you have a song with 100 frames and `width_in_frames` is 50, then
    /// each waveform data frame will correspond to 2 frames of the original
    /// song.
    int width_in_frames;

    /// Determines what GrooveWaveformInfo::data contains. Defaults to
    /// #GrooveWaveformFormatU8
    enum GrooveWaveformFormat format;

    /// maximum number of bytes to store in the queue waiting to be retrieved
    /// with ::groove_waveform_info_get. Defaults to `INT_MAX`, meaning that
    /// the waveform sink will cause the decoder to decode the entire playlist
    /// (depending on #GrooveFillMode setting and other sinks).
    int info_queue_size_bytes;

    /// how big the sink buffer should be
    /// ::groove_waveform_create defaults this to 64KB
    int sink_buffer_size_bytes;
};

GROOVE_EXPORT struct GrooveWaveform *groove_waveform_create(struct Groove *);
GROOVE_EXPORT void groove_waveform_destroy(struct GrooveWaveform *waveform);

/// Once you attach, you must detach before destroying the playlist
/// Consider setting the GrooveFile::override_duration field of GrooveFile
/// to ensure accurate waveforms.
GROOVE_EXPORT int groove_waveform_attach(struct GrooveWaveform *waveform,
        struct GroovePlaylist *playlist);
GROOVE_EXPORT int groove_waveform_detach(struct GrooveWaveform *waveform);

/// returns < 0 on error, 0 on aborted (block=1) or no info ready (block=0),
/// 1 on info returned
/// Call ::groove_waveform_info_unref when done.
GROOVE_EXPORT int groove_waveform_info_get(struct GrooveWaveform *waveform,
        struct GrooveWaveformInfo **info, int block);

/// returns < 0 on error, 0 on no info ready, 1 on info ready
/// if block is 1, block until info is ready
GROOVE_EXPORT int groove_waveform_info_peek(struct GrooveWaveform *waveform, int block);

/// get the position of the detect head
/// both the current playlist item and the position in seconds in the playlist
/// item are given. item will be set to NULL if the playlist is empty
/// you may pass NULL for item or seconds
GROOVE_EXPORT void groove_waveform_position(struct GrooveWaveform *waveform,
        struct GroovePlaylistItem **item, double *seconds);

GROOVE_EXPORT void groove_waveform_info_ref(struct GrooveWaveformInfo *info);
GROOVE_EXPORT void groove_waveform_info_unref(struct GrooveWaveformInfo *info);

#endif
