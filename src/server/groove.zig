const SoundIo = @import("soundio.zig").SoundIo;

pub const Groove = opaque {
    pub const version = groove_version;
    extern fn groove_version() [*:0]const u8;

    pub fn create() Error!*Groove {
        var result: *Groove = undefined;
        try wrapError(groove_create(&result));
        return result;
    }
    extern fn groove_create(groove: **Groove) CError;

    pub fn file_create(groove: *Groove) error{OutOfMemory}!*File {
        return groove_file_create(groove) orelse return error.OutOfMemory;
    }
    extern fn groove_file_create(*Groove) ?*File;

    pub fn playlist_create(groove: *Groove) error{OutOfMemory}!*Playlist {
        return groove_playlist_create(groove) orelse return error.OutOfMemory;
    }
    extern fn groove_playlist_create(*Groove) ?*Playlist;

    pub fn encoder_create(groove: *Groove) error{OutOfMemory}!*Encoder {
        return groove_encoder_create(groove) orelse return error.OutOfMemory;
    }
    extern fn groove_encoder_create(*Groove) ?*Encoder;

    pub fn player_create(groove: *Groove) error{OutOfMemory}!*Player {
        return groove_player_create(groove) orelse return error.OutOfMemory;
    }
    extern fn groove_player_create(groove: *Groove) ?*Player;

    pub const set_logging = groove_set_logging;
    extern fn groove_set_logging(level: LOG) void;

    pub const LOG = enum(c_int) {
        QUIET = -8,
        ERROR = 16,
        WARNING = 24,
        INFO = 32,
    };

    pub const FillMode = enum(c_int) {
        /// With this behavior, the playlist will stop decoding audio when any attached
        /// sink is full, and then resume decoding audio when every sink is not full.
        /// This is the default behavior.
        AnySinkFull,
        /// With this behavior, the playlist will decode audio if any sinks
        /// are not full. If any sinks do not drain fast enough the data will buffer up
        /// in the playlist.
        EverySinkFull,
    };

    pub const BUFFER = enum(c_int) { NO, YES, END };

    pub const CError = enum(c_int) {
        None = 0,
        NoMem = -1,
        InvalidSampleFormat = -2,
        SystemResources = -3,
        Invalid = -4,
        SinkNotFound = -5,
        NoChanges = -6,
        FileSystem = -7,
        UnknownFormat = -8,
        TooManyStreams = -9,
        Encoding = -10,
        Decoding = -11,
        StreamNotFound = -12,
        DecoderNotFound = -13,
        InvalidChannelLayout = -14,
        FileNotFound = -15,
        Permissions = -16,
        EncoderNotFound = -17,
        OpeningDevice = -18,
        DeviceParams = -19,
    };

    pub const Error = error{
        OutOfMemory,
        InvalidSampleFormat,
        SystemResources,
        SinkNotFound,
        NoChanges,
        FileSystem,
        UnknownFormat,
        TooManyStreams,
        Encoding,
        Decoding,
        StreamNotFound,
        DecoderNotFound,
        InvalidChannelLayout,
        FileNotFound,
        Permissions,
        EncoderNotFound,
        OpeningDevice,
        DeviceParams,
    };

    fn wrapError(err: CError) Error!void {
        switch (err) {
            .None => return,
            .NoMem => return error.OutOfMemory,
            .InvalidSampleFormat => return error.InvalidSampleFormat,
            .SystemResources => return error.SystemResources,
            .Invalid => unreachable,
            .SinkNotFound => return error.SinkNotFound,
            .NoChanges => return error.NoChanges,
            .FileSystem => return error.FileSystem,
            .UnknownFormat => return error.UnknownFormat,
            .TooManyStreams => return error.TooManyStreams,
            .Encoding => return error.Encoding,
            .Decoding => return error.Decoding,
            .StreamNotFound => return error.StreamNotFound,
            .DecoderNotFound => return error.DecoderNotFound,
            .InvalidChannelLayout => return error.InvalidChannelLayout,
            .FileNotFound => return error.FileNotFound,
            .Permissions => return error.Permissions,
            .EncoderNotFound => return error.EncoderNotFound,
            .OpeningDevice => return error.OpeningDevice,
            .DeviceParams => return error.DeviceParams,
        }
    }

    pub const File = extern struct {
        dirty: c_int,
        filename: [*:0]const u8,
        override_duration: f64,

        pub const destroy = groove_file_destroy;
        extern fn groove_file_destroy(file: *File) void;

        pub fn open(file: *File, filename: [*:0]const u8, filename_hint: [*:0]const u8) Error!void {
            return wrapError(groove_file_open(file, filename, filename_hint));
        }
        extern fn groove_file_open(file: *File, filename: [*:0]const u8, filename_hint: [*:0]const u8) CError;

        pub const close = groove_file_close;
        extern fn groove_file_close(file: *File) void;

        pub const metadata_get = groove_file_metadata_get;
        extern fn groove_file_metadata_get(file: *File, key: [*:0]const u8, prev: ?*const Tag, flags: c_int) ?*Tag;

        extern fn groove_file_open_custom(file: *File, custom_io: *CustomIo, filename_hint: [*:0]const u8) CError;
        extern fn groove_file_metadata_set(file: *File, key: [*:0]const u8, value: [*:0]const u8, flags: c_int) CError;
        /// a comma separated list of short names for the format
        extern fn groove_file_short_names(file: *File) [*:0]const u8;
        /// write changes made to metadata to disk.
        /// return < 0 on error
        extern fn groove_file_save(file: *File) CError;
        extern fn groove_file_save_as(file: *File, filename: [*:0]const u8) CError;
        /// main audio stream duration in seconds. note that this relies on a
        /// combination of format headers and heuristics. It can be inaccurate.
        /// The most accurate way to learn the duration of a file is to use
        /// GrooveLoudnessDetector
        extern fn groove_file_duration(file: *File) f64;
        /// get the audio format of the main audio stream of a file
        extern fn groove_file_audio_format(file: *File, audio_format: *AudioFormat) void;
    };

    pub const Playlist = extern struct {
        head: *Item,
        tail: *Item,
        gain: f64,

        pub const destroy = groove_playlist_destroy;
        extern fn groove_playlist_destroy(playlist: *Playlist) void;

        pub const play = groove_playlist_play;
        extern fn groove_playlist_play(playlist: *Playlist) void;

        pub const pause = groove_playlist_pause;
        extern fn groove_playlist_pause(playlist: *Playlist) void;

        pub const seek = groove_playlist_seek;
        extern fn groove_playlist_seek(playlist: *Playlist, item: *Item, seconds: f64) void;

        /// Once you add a file to the playlist, you must not destroy it until you first
        /// remove it from the playlist.
        /// returns the newly created playlist item, or NULL if out of memory.
        pub fn insert(
            playlist: *Playlist,
            file: *File,
            /// see GroovePlaylistItem structure. use 1.0 for no adjustment.
            gain: f64,
            /// see GroovePlaylistItem structure. use 1.0 for no adjustment.
            peak: f64,
            /// the item to insert before. if NULL, you will append to the playlist.
            next: ?*Item,
        ) error{OutOfMemory}!*Item {
            return groove_playlist_insert(playlist, file, gain, peak, next) orelse
                return error.OutOfMemory;
        }
        extern fn groove_playlist_insert(playlist: *Playlist, file: *File, gain: f64, peak: f64, next: ?*Item) ?*Item;

        pub const remove = groove_playlist_remove;
        extern fn groove_playlist_remove(playlist: *Playlist, item: *Item) void;

        pub const position = groove_playlist_position;
        extern fn groove_playlist_position(playlist: *Playlist, item: **Item, seconds: *f64) void;

        pub fn playing(playlist: *Playlist) bool {
            return groove_playlist_playing(playlist) != 0;
        }
        extern fn groove_playlist_playing(playlist: *Playlist) c_int;

        pub const clear = groove_playlist_clear;
        extern fn groove_playlist_clear(playlist: *Playlist) void;

        pub const count = groove_playlist_count;
        extern fn groove_playlist_count(playlist: *Playlist) c_int;

        pub const set_gain = groove_playlist_set_gain;
        extern fn groove_playlist_set_gain(playlist: *Playlist, gain: f64) void;

        pub const set_item_gain_peak = groove_playlist_set_item_gain_peak;
        extern fn groove_playlist_set_item_gain_peak(playlist: *Playlist, item: *Item, gain: f64, peak: f64) void;

        pub const set_fill_mode = groove_playlist_set_fill_mode;
        extern fn groove_playlist_set_fill_mode(playlist: *Playlist, mode: FillMode) void;

        pub const Item = extern struct {
            file: *File,
            gain: f64,
            peak: f64,
            prev: ?*Item = null,
            next: ?*Item = null,
        };
    };

    pub const Encoder = extern struct {
        /// The desired audio format to encode.
        /// ::groove_encoder_create defaults these to 44100 Hz,
        /// signed 16-bit int, stereo.
        /// These are preferences; if a setting cannot be used, a substitute will be
        /// used instead. actual_audio_format is set to the actual values.
        target_audio_format: AudioFormat,
        /// Select encoding quality by choosing a target bit rate in bits per
        /// second. Note that typically you see this expressed in "kbps", such
        /// as 320kbps or 128kbps. Surprisingly, in this circumstance 1 kbps is
        /// 1000 bps, *not* 1024 bps as you would expect.
        /// ::groove_encoder_create defaults this to 256000
        bit_rate: c_int,
        /// optional - choose a short name for the format
        /// to help libgroove guess which format to use
        /// use `avconv -formats` to get a list of possibilities
        format_short_name: [*:0]const u8,
        /// optional - choose a short name for the codec
        /// to help libgroove guess which codec to use
        /// use `avconv -codecs` to get a list of possibilities
        codec_short_name: [*:0]const u8,
        /// optional - provide an example filename
        /// to help libgroove guess which format/codec to use
        filename: [*:0]const u8,
        /// optional - provide a mime type string
        /// to help libgroove guess which format/codec to use
        mime_type: [*:0]const u8,
        /// how big the sink buffer should be
        /// ::groove_encoder_create defaults this to 64KB
        sink_buffer_size_bytes: c_int,
        /// how big the encoded audio buffer should be, in bytes
        /// ::groove_encoder_create defaults this to 16384
        encoded_buffer_size: c_int,
        /// This volume adjustment to make to this player.
        /// It is recommended that you leave this at 1.0 and instead adjust the
        /// gain of the underlying playlist.
        /// If you want to change this value after you have already attached the
        /// sink to the playlist, you must use ::groove_encoder_set_gain.
        /// float format. Defaults to 1.0
        gain: f64,
        /// read-only. set when attached and cleared when detached
        playlist: *Playlist,
        /// read-only. set to the actual format you get when you attach to a
        /// playlist. ideally will be the same as target_audio_format but might
        /// not be.
        actual_audio_format: AudioFormat,

        /// detach before destroying
        pub const destroy = groove_encoder_destroy;
        extern fn groove_encoder_destroy(encoder: *Encoder) void;

        /// once you attach, you must detach before destroying the playlist
        /// at playlist begin, format headers are generated. when end of playlist is
        /// reached, format trailers are generated.
        pub fn attach(encoder: *Encoder, playlist: *Playlist) Error!void {
            return wrapError(groove_encoder_attach(encoder, playlist));
        }
        extern fn groove_encoder_attach(encoder: *Encoder, playlist: *Playlist) CError;

        pub const detach = groove_encoder_detach;
        extern fn groove_encoder_detach(encoder: *Encoder) CError;

        pub fn buffer_get(encoder: *Encoder, buffer: *?*Buffer, block: bool) Error!BUFFER {
            const rc = groove_encoder_buffer_get(encoder, buffer, @intFromBool(block));
            if (rc < 0) try wrapError(@as(CError, @enumFromInt(rc)));
            return @as(BUFFER, @enumFromInt(rc));
        }
        /// returns < 0 on error, #GROOVE_BUFFER_NO on aborted (block=1) or no buffer
        /// ready (block=0), #GROOVE_BUFFER_YES on buffer returned, and GROOVE_BUFFER_END
        /// on end of playlist.
        /// buffer is always set to either a valid GrooveBuffer or `NULL`.
        extern fn groove_encoder_buffer_get(encoder: *Encoder, buffer: *?*Buffer, block: c_int) c_int;

        /// returns < 0 on error, 0 on no buffer ready, 1 on buffer ready
        /// if block is 1, block until buffer is ready
        pub const buffer_peek = groove_encoder_buffer_peek;
        extern fn groove_encoder_buffer_peek(encoder: *Encoder, block: c_int) c_int;

        /// see docs for groove_file_metadata_get
        pub const metadata_get = groove_encoder_metadata_get;
        extern fn groove_encoder_metadata_get(encoder: *Encoder, key: [*:0]const u8, prev: ?*const Tag, flags: c_int) ?*Tag;

        /// see docs for ::groove_file_metadata_set
        pub const metadata_set = groove_encoder_metadata_set;
        extern fn groove_encoder_metadata_set(encoder: *Encoder, key: [*:0]const u8, value: [*:0]const u8, flags: c_int) CError;

        /// get the position of the encode head
        /// both the current playlist item and the position in seconds in the playlist
        /// item are given. item will be set to NULL if the playlist is empty
        /// you may pass NULL for item or seconds
        pub const position = groove_encoder_position;
        extern fn groove_encoder_position(encoder: *Encoder, item: ?**Playlist.Item, seconds: ?*f64) void;

        /// See the gain property of GrooveSink. It is recommended that you leave this
        /// at 1.0 and instead adjust the gain of the playlist.
        /// returns 0 on success, < 0 on error
        pub const set_gain = groove_encoder_set_gain;
        extern fn groove_encoder_set_gain(encoder: *Encoder, gain: f64) CError;
    };

    pub const Player = extern struct {
        device: *SoundIo.Device,
        gain: f64,
        name: [*:0]const u8,
        playlist: *Playlist,

        pub const destroy = groove_player_destroy;
        extern fn groove_player_destroy(player: *Player) void;

        pub fn attach(player: *Player, playlist: *Playlist) Error!void {
            return wrapError(groove_player_attach(player, playlist));
        }
        extern fn groove_player_attach(player: *Player, playlist: *Playlist) CError;

        pub const detach = groove_player_detach;
        extern fn groove_player_detach(player: *Player) CError;

        /// get the position of the play head
        /// both the current playlist item and the position in seconds in the playlist
        /// item are given. item will be set to NULL if the playlist is empty
        /// you may pass NULL for item or seconds
        /// seconds might be negative, to compensate for the latency of the sound
        /// card buffer.
        pub const position = groove_player_position;
        extern fn groove_player_position(player: *Player, item: ?*?*Playlist.Item, seconds: ?*f64) void;

        pub const event_get = groove_player_event_get;
        extern fn groove_player_event_get(player: *Player, event: *Event, block: c_int) c_int;

        pub const event_peek = groove_player_event_peek;
        extern fn groove_player_event_peek(player: *Player, block: c_int) c_int;

        pub const event_wakeup = groove_player_event_wakeup;
        extern fn groove_player_event_wakeup(player: *Player) void;

        pub const set_gain = groove_player_set_gain;
        extern fn groove_player_set_gain(player: *Player, gain: f64) CError;

        pub const get_device_audio_format = groove_player_get_device_audio_format;
        extern fn groove_player_get_device_audio_format(player: *Player, out_audio_format: *AudioFormat) void;

        pub const Event = extern union {
            type: Type,

            pub const Type = enum(c_int) {
                NOWPLAYING = 0,
                BUFFERUNDERRUN = 1,
                DEVICE_CLOSED = 2,
                DEVICE_OPENED = 3,
                DEVICE_OPEN_ERROR = 4,
                STREAM_ERROR = 5,
                END_OF_PLAYLIST = 6,
                WAKEUP = 7,
            };
        };
    };

    pub const Tag = opaque {
        pub const key = groove_tag_key;
        extern fn groove_tag_key(tag: *Tag) [*:0]const u8;

        pub const value = groove_tag_value;
        pub extern fn groove_tag_value(tag: *Tag) [*:0]const u8;
    };

    pub const Buffer = extern struct {
        /// read-only.
        /// * for interleaved audio, data[0] is the buffer.
        /// * for planar audio, each channel has a separate data pointer.
        /// * for encoded audio, data[0] is the encoded buffer.
        data: [*][*]u8,

        /// read-only
        format: AudioFormat,

        /// read-only
        /// number of audio frames described by this buffer
        /// for encoded audio, this is unknown and set to 0.
        frame_count: c_int,

        /// read-only
        /// when encoding, if item is NULL, this is a format header or trailer.
        /// otherwise, this is encoded audio for the item specified.
        /// when decoding, item is never NULL.
        item: *Playlist.Item,
        /// read-only
        pos: f64,

        /// read-only
        /// total number of bytes contained in this buffer
        size: c_int,

        /// read-only
        /// presentation time stamp of the buffer
        pts: u64,

        pub const ref = groove_buffer_ref;
        extern fn groove_buffer_ref(buffer: *Buffer) void;

        pub const unref = groove_buffer_unref;
        extern fn groove_buffer_unref(buffer: *Buffer) void;
    };

    pub const CustomIo = opaque {};

    pub const AudioFormat = extern struct {
        sample_rate: c_int,
        layout: SoundIo.ChannelLayout,
        format: SoundIo.Format,
        /// 0 - nonplanar, otherwise planar
        is_planar: c_int,
    };
};
