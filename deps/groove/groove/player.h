/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_PLAYER_H
#define GROOVE_PLAYER_H

#include <groove/groove.h>

/// use this to make a playlist utilize your speakers
enum GroovePlayerEventType {
    /// when the currently playing track changes.
    GROOVE_EVENT_NOWPLAYING,

    /// when something tries to read from an empty buffer
    GROOVE_EVENT_BUFFERUNDERRUN,

    /// when the audio device is closed
    GROOVE_EVENT_DEVICE_CLOSED,

    /// when the audio device is opened
    GROOVE_EVENT_DEVICE_OPENED,

    /// when the audio device gets an error re-opening
    /// in this event the only recourse you have is to detach the player
    /// and re-attach it.
    GROOVE_EVENT_DEVICE_OPEN_ERROR,

    /// when this event occurs the only recourse you have is to detach the
    /// player and re-attach it.
    GROOVE_EVENT_STREAM_ERROR,

    /// when the end of playlist is hit
    GROOVE_EVENT_END_OF_PLAYLIST,

    /// user requested wakeup
    GROOVE_EVENT_WAKEUP
};

union GroovePlayerEvent {
    enum GroovePlayerEventType type;
};

struct GroovePlayer {
    /// Set this to the device you want to open.
    struct SoundIoDevice *device;

    /// Volume adjustment to make to this player.
    /// It is recommended that you leave this at 1.0 and instead adjust the
    /// gain of the underlying playlist.
    /// If you want to change this value after you have already attached the
    /// sink to the playlist, you must use ::groove_player_set_gain.
    /// float format. Defaults to 1.0
    double gain;

    /// Stream name. Used for some system's volume mixer interfaces.
    const char *name;

    /// Read-only. Set when you call ::groove_player_attach and cleared when
    /// you call ::groove_player_detach
    struct GroovePlaylist *playlist;
};

GROOVE_EXPORT struct GroovePlayer *groove_player_create(struct Groove *groove);
GROOVE_EXPORT void groove_player_destroy(struct GroovePlayer *player);

/// Attaches the player to the playlist instance and opens the device to
/// begin playback.
/// Internally this creates a GrooveSink and sends the samples to the device.
/// you must detach a player before destroying it or the playlist it is
/// attached to
/// returns 0 on success, < 0 on error
GROOVE_EXPORT int groove_player_attach(struct GroovePlayer *player,
        struct GroovePlaylist *playlist);
/// returns 0 on success, < 0 on error
GROOVE_EXPORT int groove_player_detach(struct GroovePlayer *player);

/// get the position of the play head
/// both the current playlist item and the position in seconds in the playlist
/// item are given. item will be set to NULL if the playlist is empty
/// you may pass NULL for item or seconds
/// seconds might be negative, to compensate for the latency of the sound
/// card buffer.
GROOVE_EXPORT void groove_player_position(struct GroovePlayer *player,
        struct GroovePlaylistItem **item, double *seconds);

/// returns < 0 on error, 0 on no event ready, 1 on got event
GROOVE_EXPORT int groove_player_event_get(struct GroovePlayer *player,
        union GroovePlayerEvent *event, int block);
/// returns < 0 on error, 0 on no event ready, 1 on event ready
/// if block is 1, block until event is ready
GROOVE_EXPORT int groove_player_event_peek(struct GroovePlayer *player, int block);

/// wakes up a blocking call to groove_player_event_get or
/// groove_player_event_peek with a GROOVE_EVENT_WAKEUP.
GROOVE_EXPORT void groove_player_event_wakeup(struct GroovePlayer *player);

/// See the gain property of GrooveSink. It is recommended that you leave this
/// at 1.0 and instead adjust the gain of the playlist.
/// returns 0 on success, < 0 on error
GROOVE_EXPORT int groove_player_set_gain(struct GroovePlayer *player, double gain);

/// When you set the use_exact_audio_format field to 1, the audio device is
/// closed and re-opened as necessary. When this happens, a
/// #GROOVE_EVENT_DEVICEREOPENED event is emitted, and you can use this function
/// to discover the audio format of the device.
GROOVE_EXPORT void groove_player_get_device_audio_format(struct GroovePlayer *player,
        struct GrooveAudioFormat *out_audio_format);

#endif
