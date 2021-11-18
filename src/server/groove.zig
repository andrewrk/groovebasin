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

    pub const set_logging = groove_set_logging;
    extern fn groove_set_logging(level: LOG) void;

    pub const LOG = enum(c_int) {
        QUIET = -8,
        ERROR = 16,
        WARNING = 24,
        INFO = 32,
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

    pub const File = opaque {
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
        //extern fn groove_file_audio_format(file: *File, audio_format: *AudioFormat) void;
    };

    pub const Tag = opaque {
        pub const key = groove_tag_key;
        extern fn groove_tag_key(tag: *Tag) [*:0]const u8;

        pub const value = groove_tag_value;
        pub extern fn groove_tag_value(tag: *Tag) [*:0]const u8;
    };

    pub const CustomIo = opaque {};

    //pub const AudioFormat = extern struct {
    //    sample_rate: c_int,
    //    layout: SoundIo.ChannelLayout,
    //    format: SoundIo.Format,
    //    is_planar: c_int,
    //};
};
