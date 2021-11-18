/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_GROOVE_H
#define GROOVE_GROOVE_H

#include <stdint.h>
#include <soundio/soundio.h>

/// \cond
#ifdef __cplusplus
#define GROOVE_EXTERN_C extern "C"
#else
#define GROOVE_EXTERN_C
#endif

#if defined(_WIN32)
#if defined(GROOVE_BUILDING_LIBRARY)
#define GROOVE_EXPORT GROOVE_EXTERN_C __declspec(dllexport)
#else
#define GROOVE_EXPORT GROOVE_EXTERN_C __declspec(dllimport)
#endif
#else
#define GROOVE_EXPORT GROOVE_EXTERN_C __attribute__((visibility ("default")))
#endif
/// \endcond

/** \mainpage
 *
 * \section intro_sec Overview
 *
 * * groove.h - main header
 * * player.h - attach a GroovePlayer to a GroovePlaylist to play audio over
 *   a system audio device.
 * * encoder.h - attach a GrooveEncoder to a GroovePlaylist to encode audio.
 * * fingerprinter.h - attach a GrooveFingerprinter to a GroovePlaylist to
 *   calculate acoustid fingerprints.
 * * loudness.h - attach a GrooveLoudnessDetector to a GroovePlaylist to
 *   calculate EBU R128 loudness of tracks and albums.
 */

/** \example playlist.c
 * play several files in a row and then exit
 */

/** \example transcode.c
 * transcode one or more files into one output file
 */

/** \example fingerprint.c
 * compute the acoustid of a list of songs
 */

/** \example metadata.c
 * read or update metadata in a media file
 */

/** \example replaygain.c
 * replaygain scanner
 */

/** \example waveform.c
 * replaygain scanner
 */

/// See also ::groove_strerror
enum GrooveError {
    GrooveErrorNone                 =   0,
    GrooveErrorNoMem                =  -1,
    GrooveErrorInvalidSampleFormat  =  -2,
    GrooveErrorSystemResources      =  -3,
    GrooveErrorInvalid              =  -4,
    GrooveErrorSinkNotFound         =  -5,
    GrooveErrorNoChanges            =  -6,
    GrooveErrorFileSystem           =  -7,
    GrooveErrorUnknownFormat        =  -8,
    GrooveErrorTooManyStreams       =  -9,
    GrooveErrorEncoding             = -10,
    GrooveErrorDecoding             = -11,
    GrooveErrorStreamNotFound       = -12,
    GrooveErrorDecoderNotFound      = -13,
    GrooveErrorInvalidChannelLayout = -14,
    GrooveErrorFileNotFound         = -15,
    GrooveErrorPermissions          = -16,
    GrooveErrorEncoderNotFound      = -17,
    GrooveErrorOpeningDevice        = -18,
    GrooveErrorDeviceParams         = -19,
};

/// Specifies when the sink will stop decoding.
enum GrooveFillMode {
    /// With this behavior, the playlist will stop decoding audio when any attached
    /// sink is full, and then resume decoding audio when every sink is not full.
    /// This is the default behavior.
    GrooveFillModeAnySinkFull,
    /// With this behavior, the playlist will decode audio if any sinks
    /// are not full. If any sinks do not drain fast enough the data will buffer up
    /// in the playlist.
    GrooveFillModeEverySinkFull,
};

enum GrooveSinkFlags {
    GrooveSinkFlagPlanarOk = 0x1,
    GrooveSinkFlagInterleavedOk = 0x2,
};

#define GROOVE_LOG_QUIET    -8
#define GROOVE_LOG_ERROR    16
#define GROOVE_LOG_WARNING  24
#define GROOVE_LOG_INFO     32

/// flags to groove_file_metadata_*
#define GROOVE_TAG_MATCH_CASE      1
#define GROOVE_TAG_DONT_OVERWRITE 16

/// If the entry already exists, append to it.  Note that no
/// delimiter is added, the strings are simply concatenated.
#define GROOVE_TAG_APPEND         32



#define GROOVE_BUFFER_NO  0
#define GROOVE_BUFFER_YES 1
#define GROOVE_BUFFER_END 2

#define GROOVE_SEEK_SIZE 0x10000
#define GROOVE_SEEK_FORCE 0x20000

struct Groove;

struct GrooveAudioFormat {
    int sample_rate;
    struct SoundIoChannelLayout layout;
    enum SoundIoFormat format;
    /// 0 - nonplanar, otherwise planar
    int is_planar;
};

struct GrooveFile {
    /// read-only
    int dirty;

    /// read-only
    const char *filename;

    /// This must only be set when no GroovePlaylistItem refers to this file.
    /// ::groove_file_open estimates the duration, but it can be inaccurate.
    /// If you know the correct duration, you can supply it here.
    double override_duration;
};

struct GrooveCustomIo {
    /// set to whatever you want. Defaults to NULL.
    void *userdata;
    /// A function for refilling the buffer, may be NULL.
    int (*read_packet)(struct GrooveCustomIo *, uint8_t *buf, int buf_size);
    /// A function for writing the buffer contents, may be NULL.
    int (*write_packet)(struct GrooveCustomIo *, uint8_t *buf, int buf_size);
    /// A function for seeking to specified byte position, may be NULL.
    /// `whence` can be:
    /// * `SEEK_SET` - see fseek documentation.
    /// * `SEEK_CUR` - see fseek documentation.
    /// * `SEEK_END` - see fseek documentation.
    /// * `GROOVE_SEEK_SIZE` - (optional) if it's OR'd to whence, seek function
    ///   should return the stream size without seeking.
    /// * `GROOVE_SEEK_FORCE` - (optional) if it's OR'd to whence,
    ///   seek by any means even if it's really slow.
    /// If you don't recognize the whence parameter, return -1.
    int64_t (*seek)(struct GrooveCustomIo *, int64_t offset, int whence);
};

struct GrooveTag;

struct GroovePlaylistItem {
    /// read-only
    struct GrooveFile *file;

    /// Read-only. A volume adjustment in float format to apply to the file when it plays.
    /// This is typically used for loudness compensation, for example ReplayGain.
    /// To convert from dB to float, use exp(log(10) * 0.05 * dB_value)
    /// Modify with ::groove_playlist_set_item_gain_peak
    double gain;

    /// Read-only. The sample peak of this playlist item is assumed to be 1.0 in float
    /// format. If you know for certain that the peak is less than 1.0, you
    /// may set this value which may allow the volume adjustment to use
    /// a pure amplifier rather than a compressor. This results in slightly
    /// better audio quality.
    /// Modify with ::groove_playlist_set_item_gain_peak
    double peak;

    /// A GroovePlaylist is a doubly linked list. Use these fields to
    /// traverse the list.
    /// See also ::groove_playlist_remove
    /// See also ::groove_playlist_insert
    struct GroovePlaylistItem *prev;
    struct GroovePlaylistItem *next;
};

struct GroovePlaylist {
    /// read-only. Head of the doubly linked list which is the playlist.
    /// See also ::groove_playlist_insert
    /// See also ::groove_playlist_remove
    struct GroovePlaylistItem *head;
    /// read-only. Tail of the doubly linked list which is the playlist.
    /// See also ::groove_playlist_insert
    /// See also ::groove_playlist_remove
    struct GroovePlaylistItem *tail;

    /// read-only. volume adjustment in float format which applies to all playlist items
    /// and all sinks. defaults to 1.0.
    /// Modify with ::groove_playlist_set_gain
    double gain;
};

struct GrooveBuffer {
    /// read-only.
    /// * for interleaved audio, data[0] is the buffer.
    /// * for planar audio, each channel has a separate data pointer.
    /// * for encoded audio, data[0] is the encoded buffer.
    uint8_t **data;

    /// read-only
    struct GrooveAudioFormat format;

    /// read-only
    /// number of audio frames described by this buffer
    /// for encoded audio, this is unknown and set to 0.
    int frame_count;

    /// read-only
    /// when encoding, if item is NULL, this is a format header or trailer.
    /// otherwise, this is encoded audio for the item specified.
    /// when decoding, item is never NULL.
    struct GroovePlaylistItem *item;
    /// read-only
    double pos;

    /// read-only
    /// total number of bytes contained in this buffer
    int size;

    /// read-only
    /// presentation time stamp of the buffer
    uint64_t pts;
};

/// use this to get access to a realtime raw audio buffer
/// for example you could use it to draw a waveform or other visualization
/// GroovePlayer uses this internally to get the audio buffer for playback
struct GrooveSink {
    /// Set the allowed sample rates buffers in this sink will have. You may
    /// leave this to the default `NULL` to allow any sample rate.
    /// If the source audio format does not match any of these sample rates,
    /// sample rate conversion will be performed to convert the sample rate to
    /// GrooveSink::default_sample_rate.
    /// See also ::groove_sink_set_only_format
    struct SoundIoSampleRateRange *sample_rates;
    /// See also ::groove_sink_set_only_format
    int sample_rate_count;
    /// If the source audio format does not match, sample rate conversion will
    /// be performed to convert the sample rate to this value.
    /// See also ::groove_sink_set_only_format
    int sample_rate_default;

    /// Set the allowed channel layouts buffers in this sink will have. You may
    /// leave this to the default `NULL` to allow any channel layouts.
    /// If the source channel layout does not match any of these channel
    /// layouts, the audio will be remapped to fit
    /// GrooveSink::channel_layout_default
    /// See also ::groove_sink_set_only_format
    struct SoundIoChannelLayout *channel_layouts;
    /// See also ::groove_sink_set_only_format
    int channel_layout_count;
    /// If the source channel layout does not match, audio will be remapped to
    /// fit this channel layout. This should point to one of
    /// GrooveSink::channel_layouts.
    /// See also ::groove_sink_set_only_format
    struct SoundIoChannelLayout channel_layout_default;

    /// Set the allowed sample formats buffers in this sink will have. You may
    /// leave this to the default `NULL` to allow any sample format.
    /// If the source sample format does not match any of these sample formats,
    /// sample format conversion will be performed to convert the sample format
    /// to GrooveSink::sample_format_default.
    /// See also ::groove_sink_set_only_format
    enum SoundIoFormat *sample_formats;
    /// See also ::groove_sink_set_only_format
    int sample_format_count;
    /// If the source sample format does not match, sample format conversion
    /// will be performed to convert the sample format to this.
    /// See also ::groove_sink_set_only_format
    enum SoundIoFormat sample_format_default;

    /// See #GrooveSinkFlags
    /// * If #GrooveSinkFlagPlanarOk is set, buffers in this sink may be planar.
    /// * If #GrooveSinkFlagInterleavedOk is set, buffers in this sink may be
    ///   interleaved.
    /// Leaving both of these flags unset is the same as having them both set.
    /// See also ::groove_sink_set_only_format
    uint32_t flags;

    /// If you leave this to its default of 0, frames pulled from the sink
    /// will have sample count determined by efficiency.
    /// If you set this to a positive number, frames pulled from the sink
    /// will always have this number of samples.
    int buffer_sample_count;

    /// ::groove_sink_create defaults this to 64KB
    int buffer_size_bytes;

    /// This volume adjustment only applies to this sink.
    /// It is recommended that you leave this at 1.0 and instead adjust the
    /// gain of the playlist.
    /// If you want to change this value after you have already attached the
    /// sink to the playlist, you must use ::groove_sink_set_gain.
    /// float format. Defaults to 1.0
    double gain;

    /// set to whatever you want, defaults to `NULL`.
    void *userdata;
    /// called when the audio queue is flushed. For example, if you seek to a
    /// different location in the song.
    void (*flush)(struct GrooveSink *);
    /// called when a playlist item is deleted. Take this opportunity to remove
    /// all your references to the GroovePlaylistItem.
    void (*purge)(struct GrooveSink *, struct GroovePlaylistItem *);
    /// called when the playlist is paused
    void (*pause)(struct GrooveSink *);
    /// called when the playlist is played
    void (*play)(struct GrooveSink *);
    /// Called when a buffer is put into the sink. This is called from a thread
    /// context in which it is undefined behavior to call any libgroove functions.
    void (*filled)(struct GrooveSink *);

    /// read-only. set when you call ::groove_sink_attach. cleared when you call
    /// ::groove_sink_detach
    struct GroovePlaylist *playlist;
};


/// You should only need one of these Groove contexts.
///
/// Possible errors:
/// * GrooveErrorSystemResources
/// * GrooveErrorNoMem
GROOVE_EXPORT int groove_create(struct Groove **groove);
GROOVE_EXPORT void groove_destroy(struct Groove *groove);

GROOVE_EXPORT const char *groove_strerror(int error);

/// enable/disable logging of errors
GROOVE_EXPORT void groove_set_logging(int level);


/// returns 1 if the audio formats have the same sample rate, channel layout,
/// and sample format. returns 0 otherwise.
GROOVE_EXPORT int groove_audio_formats_equal(
        const struct GrooveAudioFormat *a,
        const struct GrooveAudioFormat *b);

GROOVE_EXPORT int groove_version_major(void);
GROOVE_EXPORT int groove_version_minor(void);
GROOVE_EXPORT int groove_version_patch(void);
GROOVE_EXPORT const char *groove_version(void);

/// given a file path and the length of the file path, allocates a new file path
/// which is in the same directory but a random filename with the same extension.
/// the file name will start with a '.'. The caller owns the memory. The length
/// of the returned path is returned in out_len. You can pass NULL for out_len.
GROOVE_EXPORT char *groove_create_rand_name(struct Groove *,
        int *out_len, const char *file, int file_len);

GROOVE_EXPORT const char *groove_tag_key(struct GrooveTag *tag);
GROOVE_EXPORT const char *groove_tag_value(struct GrooveTag *tag);

/// See also ::groove_file_destroy
GROOVE_EXPORT struct GrooveFile *groove_file_create(struct Groove *);
GROOVE_EXPORT void groove_file_destroy(struct GrooveFile *file);
/// Open a sound file on the file system.
/// * `filename` - the file to actually open
/// * `filename_hint` - this value is sometimes used to influence the algorithm
///   which decides what format the file is (for example by looking at the extension).
///   Typically you will pass the same value for `filename` and `filename_hint`.
///
/// When done with the file, call ::groove_file_close.
GROOVE_EXPORT int groove_file_open(struct GrooveFile *file,
        const char *filename, const char *filename_hint);
GROOVE_EXPORT int groove_file_open_custom(struct GrooveFile *file,
        struct GrooveCustomIo *custom_io, const char *filename_hint);
GROOVE_EXPORT void groove_file_close(struct GrooveFile *file);

GROOVE_EXPORT struct GrooveTag *groove_file_metadata_get(struct GrooveFile *file,
        const char *key, const struct GrooveTag *prev, int flags);

/// key entry to add to metadata. will be strdup'd
/// value entry to add to metadata. will be strdup'd
///    passing NULL causes existing entry to be deleted.
/// return >= 0 on success otherwise an error code < 0
/// note that this will not save the file; you must call groove_file_save
/// to do that.
GROOVE_EXPORT int groove_file_metadata_set(struct GrooveFile *file, const char *key,
        const char *value, int flags);

/// a comma separated list of short names for the format
GROOVE_EXPORT const char *groove_file_short_names(struct GrooveFile *file);

/// write changes made to metadata to disk.
/// return < 0 on error
GROOVE_EXPORT int groove_file_save(struct GrooveFile *file);
GROOVE_EXPORT int groove_file_save_as(struct GrooveFile *file, const char *filename);

/// main audio stream duration in seconds. note that this relies on a
/// combination of format headers and heuristics. It can be inaccurate.
/// The most accurate way to learn the duration of a file is to use
/// GrooveLoudnessDetector
GROOVE_EXPORT double groove_file_duration(struct GrooveFile *file);

/// get the audio format of the main audio stream of a file
GROOVE_EXPORT void groove_file_audio_format(struct GrooveFile *file,
        struct GrooveAudioFormat *audio_format);


/// A playlist keeps its sinks full.
GROOVE_EXPORT struct GroovePlaylist *groove_playlist_create(struct Groove *);
/// This will not call ::groove_file_close on any files.
/// It will remove all playlist items and sinks from the playlist
GROOVE_EXPORT void groove_playlist_destroy(struct GroovePlaylist *playlist);


GROOVE_EXPORT void groove_playlist_play(struct GroovePlaylist *playlist);
GROOVE_EXPORT void groove_playlist_pause(struct GroovePlaylist *playlist);

GROOVE_EXPORT void groove_playlist_seek(struct GroovePlaylist *playlist,
        struct GroovePlaylistItem *item, double seconds);

/// Once you add a file to the playlist, you must not destroy it until you first
/// remove it from the playlist.
/// next: the item to insert before. if NULL, you will append to the playlist.
/// gain: see GroovePlaylistItem structure. use 1.0 for no adjustment.
/// peak: see GroovePlaylistItem structure. use 1.0 for no adjustment.
/// returns the newly created playlist item, or NULL if out of memory.
GROOVE_EXPORT struct GroovePlaylistItem *groove_playlist_insert(
        struct GroovePlaylist *playlist, struct GrooveFile *file,
        double gain, double peak,
        struct GroovePlaylistItem *next);

/// This will not call ::groove_file_close on item->file
/// Item is destroyed and the address it points to is no longer valid
GROOVE_EXPORT void groove_playlist_remove(struct GroovePlaylist *playlist,
        struct GroovePlaylistItem *item);

/// Get the position of the decode head
/// both the current playlist item and the position in seconds in the playlist
/// item are given. item will be set to NULL if the playlist is empty
/// seconds will be set to -1.0 if item is NULL.
/// you may pass NULL for item or seconds
/// Note that typically you are more interested in the position of the play
/// head, not the decode head. For example, if you have a GroovePlayer attached,
/// ::groove_player_position will give you the position of the play head.
GROOVE_EXPORT void groove_playlist_position(struct GroovePlaylist *playlist,
        struct GroovePlaylistItem **item, double *seconds);

/// return 1 if the playlist is playing; 0 if it is not.
GROOVE_EXPORT int groove_playlist_playing(struct GroovePlaylist *playlist);


/// Remove all playlist items
GROOVE_EXPORT void groove_playlist_clear(struct GroovePlaylist *playlist);

/// return the count of playlist items
GROOVE_EXPORT int groove_playlist_count(struct GroovePlaylist *playlist);

GROOVE_EXPORT void groove_playlist_set_gain(struct GroovePlaylist *playlist, double gain);

GROOVE_EXPORT void groove_playlist_set_item_gain_peak(
        struct GroovePlaylist *playlist, struct GroovePlaylistItem *item,
        double gain, double peak);

/// Use this to set the fill mode using the constants above
GROOVE_EXPORT void groove_playlist_set_fill_mode(struct GroovePlaylist *playlist,
        enum GrooveFillMode mode);

GROOVE_EXPORT void groove_buffer_ref(struct GrooveBuffer *buffer);
GROOVE_EXPORT void groove_buffer_unref(struct GrooveBuffer *buffer);

GROOVE_EXPORT struct GrooveSink *groove_sink_create(struct Groove *);
GROOVE_EXPORT void groove_sink_destroy(struct GrooveSink *sink);

/// If the sink should always convert to a single audio format, this function
/// is convenient (and avoids allocating memory).
/// This function sets the following fields based on `audio_format`:
/// * GrooveSink::sample_rates
/// * GrooveSink::sample_rate_count
/// * GrooveSink::sample_rate_default
/// * GrooveSink::channel_layouts
/// * GrooveSink::channel_layout_count
/// * GrooveSink::channel_layout_default
/// * GrooveSink::sample_formats
/// * GrooveSink::sample_format_count
/// * GrooveSink::sample_format_default
/// * GrooveSink::flags
GROOVE_EXPORT void groove_sink_set_only_format(struct GrooveSink *sink,
        const struct GrooveAudioFormat *audio_format);

/// before calling this, set audio_format
/// returns 0 on success, < 0 on error
GROOVE_EXPORT int groove_sink_attach(struct GrooveSink *sink, struct GroovePlaylist *playlist);
/// returns 0 on success, < 0 on error
GROOVE_EXPORT int groove_sink_detach(struct GrooveSink *sink);

/// returns < 0 on error, #GROOVE_BUFFER_NO on aborted (block=1) or no buffer
/// ready (block=0), #GROOVE_BUFFER_YES on buffer returned, and #GROOVE_BUFFER_END
/// on end of playlist.
/// buffer is always set to either a valid GrooveBuffer or `NULL`
GROOVE_EXPORT int groove_sink_buffer_get(struct GrooveSink *sink,
        struct GrooveBuffer **buffer, int block);

/// returns < 0 on error, 0 on no buffer ready, 1 on buffer ready
/// if block is 1, block until buffer is ready
GROOVE_EXPORT int groove_sink_buffer_peek(struct GrooveSink *sink, int block);

/// See the gain property of GrooveSink. It is recommended that you leave this
/// at 1.0 and instead adjust the gain of the playlist.
/// returns 0 on success, < 0 on error
GROOVE_EXPORT int groove_sink_set_gain(struct GrooveSink *sink, double gain);

/// Set the buffer_size_bytes field of the sink while the sink is attached.
GROOVE_EXPORT void groove_sink_set_buffer_size_bytes(struct GrooveSink *sink, int buffer_size_bytes);

/// Returns the number of bytes contained in this sink.
GROOVE_EXPORT int groove_sink_get_fill_level(struct GrooveSink *sink);

/// Returns 1 if the sink contains the end of playlist sentinel, 0 otherwise.
GROOVE_EXPORT int groove_sink_contains_end_of_playlist(struct GrooveSink *sink);


#endif
