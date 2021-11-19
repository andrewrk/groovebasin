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

    pub const set_logging = groove_set_logging;
    extern fn groove_set_logging(level: LOG) void;

    pub const LOG = enum(c_int) {
        QUIET = -8,
        ERROR = 16,
        WARNING = 24,
        INFO = 32,
    };

    pub const FillMode = enum(c_int) {
        AnySinkFull,
        EverySinkFull,
    };

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
        extern fn groove_file_short_names(file: *File) [*c]const u8;
        extern fn groove_file_save(file: *File) CError;
        extern fn groove_file_save_as(file: *File, filename: [*c]const u8) CError;
        extern fn groove_file_duration(file: *File) f64;
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

        pub const insert = groove_playlist_insert;
        extern fn groove_playlist_insert(playlist: *Playlist, file: *File, gain: f64, peak: f64, next: *Item) *Item;

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

        /// returns < 0 on error, #GROOVE_BUFFER_NO on aborted (block=1) or no buffer
        /// ready (block=0), #GROOVE_BUFFER_YES on buffer returned, and GROOVE_BUFFER_END
        /// on end of playlist.
        /// buffer is always set to either a valid GrooveBuffer or `NULL`.
        pub const buffer_get = groove_encoder_buffer_get;
        extern fn groove_encoder_buffer_get(encoder: *Encoder, buffer: *?*Buffer, block: c_int) CError;

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

pub const SoundIo = opaque {
    pub const ChannelLayout = extern struct {
        name: [*:0]const u8,
        channel_count: c_int,
        channels: [24]ChannelId,
    };

    pub const ChannelId = enum(c_int) {
        Invalid = 0,
        FrontLeft = 1,
        FrontRight = 2,
        FrontCenter = 3,
        Lfe = 4,
        BackLeft = 5,
        BackRight = 6,
        FrontLeftCenter = 7,
        FrontRightCenter = 8,
        BackCenter = 9,
        SideLeft = 10,
        SideRight = 11,
        TopCenter = 12,
        TopFrontLeft = 13,
        TopFrontCenter = 14,
        TopFrontRight = 15,
        TopBackLeft = 16,
        TopBackCenter = 17,
        TopBackRight = 18,
        BackLeftCenter = 19,
        BackRightCenter = 20,
        FrontLeftWide = 21,
        FrontRightWide = 22,
        FrontLeftHigh = 23,
        FrontCenterHigh = 24,
        FrontRightHigh = 25,
        TopFrontLeftCenter = 26,
        TopFrontRightCenter = 27,
        TopSideLeft = 28,
        TopSideRight = 29,
        LeftLfe = 30,
        RightLfe = 31,
        Lfe2 = 32,
        BottomCenter = 33,
        BottomLeftCenter = 34,
        BottomRightCenter = 35,
        MsMid = 36,
        MsSide = 37,
        AmbisonicW = 38,
        AmbisonicX = 39,
        AmbisonicY = 40,
        AmbisonicZ = 41,
        XyX = 42,
        XyY = 43,
        HeadphonesLeft = 44,
        HeadphonesRight = 45,
        ClickTrack = 46,
        ForeignLanguage = 47,
        HearingImpaired = 48,
        Narration = 49,
        Haptic = 50,
        DialogCentricMix = 51,
        Aux = 52,
        Aux0 = 53,
        Aux1 = 54,
        Aux2 = 55,
        Aux3 = 56,
        Aux4 = 57,
        Aux5 = 58,
        Aux6 = 59,
        Aux7 = 60,
        Aux8 = 61,
        Aux9 = 62,
        Aux10 = 63,
        Aux11 = 64,
        Aux12 = 65,
        Aux13 = 66,
        Aux14 = 67,
        Aux15 = 68,
    };

    pub const Format = enum(c_int) {
        Invalid = 0,
        S8 = 1,
        U8 = 2,
        S16LE = 3,
        S16BE = 4,
        U16LE = 5,
        U16BE = 6,
        S24LE = 7,
        S24BE = 8,
        U24LE = 9,
        U24BE = 10,
        S32LE = 11,
        S32BE = 12,
        U32LE = 13,
        U32BE = 14,
        Float32LE = 15,
        Float32BE = 16,
        Float64LE = 17,
        Float64BE = 18,
    };
};
