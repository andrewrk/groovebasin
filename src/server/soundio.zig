pub const SoundIo = extern struct {
    userdata: ?*anyopaque,
    on_devices_change: ?*const fn (*SoundIo) callconv(.C) void,
    on_backend_disconnect: ?*const fn (*SoundIo, c_int) callconv(.C) void,
    on_events_signal: ?*const fn (*SoundIo) callconv(.C) void,
    current_backend: Backend,
    app_name: [*:0]const u8,
    emit_rtprio_warning: ?*const fn () callconv(.C) void,
    jack_info_callback: ?*const fn ([*:0]const u8) callconv(.C) void,
    jack_error_callback: ?*const fn ([*:0]const u8) callconv(.C) void,

    pub fn create() error{OutOfMemory}!*SoundIo {
        return soundio_create() orelse return error.OutOfMemory;
    }
    extern fn soundio_create() ?*SoundIo;

    pub fn connect(soundio: *SoundIo) Error!void {
        return wrapError(soundio_connect(soundio));
    }
    extern fn soundio_connect(soundio: *SoundIo) CError;

    pub fn connect_backend(soundio: *SoundIo, backend: Backend) Error!void {
        return wrapError(soundio_connect_backend(soundio, backend));
    }
    extern fn soundio_connect_backend(soundio: *SoundIo, backend: Backend) CError;

    pub const flush_events = soundio_flush_events;
    extern fn soundio_flush_events(soundio: *SoundIo) void;

    pub const default_output_device_index = soundio_default_output_device_index;
    extern fn soundio_default_output_device_index(soundio: *SoundIo) c_int;

    pub fn get_output_device(soundio: *SoundIo, index: c_int) error{OutOfMemory}!*Device {
        return soundio_get_output_device(soundio, index) orelse return error.OutOfMemory;
    }
    extern fn soundio_get_output_device(soundio: *SoundIo, index: c_int) ?*Device;

    pub const strerror = soundio_strerror;
    extern fn soundio_strerror(@"error": c_int) [*:0]const u8;

    extern fn soundio_version_string() [*:0]const u8;
    extern fn soundio_version_major() c_int;
    extern fn soundio_version_minor() c_int;
    extern fn soundio_version_patch() c_int;
    extern fn soundio_destroy(soundio: *SoundIo) void;
    extern fn soundio_disconnect(soundio: *SoundIo) void;
    extern fn soundio_backend_name(backend: Backend) [*:0]const u8;
    extern fn soundio_backend_count(soundio: *SoundIo) c_int;
    extern fn soundio_get_backend(soundio: *SoundIo, index: c_int) Backend;
    extern fn soundio_have_backend(backend: Backend) bool;
    extern fn soundio_wait_events(soundio: *SoundIo) void;
    extern fn soundio_wakeup(soundio: *SoundIo) void;
    extern fn soundio_force_device_scan(soundio: *SoundIo) void;
    extern fn soundio_channel_layout_equal(a: [*c]const ChannelLayout, b: [*c]const ChannelLayout) bool;
    extern fn soundio_get_channel_name(id: ChannelId) [*c]const u8;
    extern fn soundio_parse_channel_id(str: [*c]const u8, str_len: c_int) ChannelId;
    extern fn soundio_channel_layout_builtin_count() c_int;
    extern fn soundio_channel_layout_get_builtin(index: c_int) [*c]const ChannelLayout;
    extern fn soundio_channel_layout_get_default(channel_count: c_int) [*c]const ChannelLayout;
    extern fn soundio_channel_layout_find_channel(layout: [*c]const ChannelLayout, channel: ChannelId) c_int;
    extern fn soundio_channel_layout_detect_builtin(layout: *ChannelLayout) bool;
    extern fn soundio_best_matching_channel_layout(preferred_layouts: [*c]const ChannelLayout, preferred_layout_count: c_int, available_layouts: [*c]const ChannelLayout, available_layout_count: c_int) [*c]const ChannelLayout;
    extern fn soundio_sort_channel_layouts(layouts: *ChannelLayout, layout_count: c_int) void;
    extern fn soundio_get_bytes_per_sample(format: Format) c_int;

    extern fn soundio_format_string(format: Format) [*c]const u8;
    extern fn soundio_input_device_count(soundio: *SoundIo) c_int;
    extern fn soundio_output_device_count(soundio: *SoundIo) c_int;
    extern fn soundio_get_input_device(soundio: *SoundIo, index: c_int) *Device;
    extern fn soundio_default_input_device_index(soundio: *SoundIo) c_int;
    extern fn soundio_device_ref(device: *Device) void;
    extern fn soundio_device_unref(device: *Device) void;
    extern fn soundio_device_equal(a: [*c]const Device, b: [*c]const Device) bool;
    extern fn soundio_device_sort_channel_layouts(device: *Device) void;
    extern fn soundio_device_supports_format(device: *Device, format: Format) bool;
    extern fn soundio_device_supports_layout(device: *Device, layout: [*c]const ChannelLayout) bool;
    extern fn soundio_device_supports_sample_rate(device: *Device, sample_rate: c_int) bool;
    extern fn soundio_device_nearest_sample_rate(device: *Device, sample_rate: c_int) c_int;
    extern fn soundio_outstream_create(device: *Device) *OutStream;
    extern fn soundio_outstream_destroy(outstream: *OutStream) void;
    extern fn soundio_outstream_open(outstream: *OutStream) c_int;
    extern fn soundio_outstream_start(outstream: *OutStream) c_int;
    extern fn soundio_outstream_begin_write(outstream: *OutStream, areas: [*c]*ChannelArea, frame_count: [*c]c_int) c_int;
    extern fn soundio_outstream_end_write(outstream: *OutStream) c_int;
    extern fn soundio_outstream_clear_buffer(outstream: *OutStream) c_int;
    extern fn soundio_outstream_pause(outstream: *OutStream, pause: bool) c_int;
    extern fn soundio_outstream_get_latency(outstream: *OutStream, out_latency: [*c]f64) c_int;
    extern fn soundio_outstream_set_volume(outstream: *OutStream, volume: f64) c_int;
    extern fn soundio_instream_create(device: *Device) *InStream;
    extern fn soundio_instream_destroy(instream: *InStream) void;
    extern fn soundio_instream_open(instream: *InStream) c_int;
    extern fn soundio_instream_start(instream: *InStream) c_int;
    extern fn soundio_instream_begin_read(instream: *InStream, areas: [*c]*ChannelArea, frame_count: [*c]c_int) c_int;
    extern fn soundio_instream_end_read(instream: *InStream) c_int;
    extern fn soundio_instream_pause(instream: *InStream, pause: bool) c_int;
    extern fn soundio_instream_get_latency(instream: *InStream, out_latency: [*c]f64) c_int;
    const struct_SoundIoRingBuffer = opaque {};
    extern fn soundio_ring_buffer_create(soundio: *SoundIo, requested_capacity: c_int) ?*struct_SoundIoRingBuffer;
    extern fn soundio_ring_buffer_destroy(ring_buffer: ?*struct_SoundIoRingBuffer) void;
    extern fn soundio_ring_buffer_capacity(ring_buffer: ?*struct_SoundIoRingBuffer) c_int;
    extern fn soundio_ring_buffer_write_ptr(ring_buffer: ?*struct_SoundIoRingBuffer) [*c]u8;
    extern fn soundio_ring_buffer_advance_write_ptr(ring_buffer: ?*struct_SoundIoRingBuffer, count: c_int) void;
    extern fn soundio_ring_buffer_read_ptr(ring_buffer: ?*struct_SoundIoRingBuffer) [*c]u8;
    extern fn soundio_ring_buffer_advance_read_ptr(ring_buffer: ?*struct_SoundIoRingBuffer, count: c_int) void;
    extern fn soundio_ring_buffer_fill_count(ring_buffer: ?*struct_SoundIoRingBuffer) c_int;
    extern fn soundio_ring_buffer_free_count(ring_buffer: ?*struct_SoundIoRingBuffer) c_int;
    extern fn soundio_ring_buffer_clear(ring_buffer: ?*struct_SoundIoRingBuffer) void;

    pub const CError = enum(c_int) {
        None = 0,
        NoMem = 1,
        InitAudioBackend = 2,
        SystemResources = 3,
        OpeningDevice = 4,
        NoSuchDevice = 5,
        Invalid = 6,
        BackendUnavailable = 7,
        Streaming = 8,
        IncompatibleDevice = 9,
        NoSuchClient = 10,
        IncompatibleBackend = 11,
        BackendDisconnected = 12,
        Interrupted = 13,
        Underflow = 14,
        EncodingString = 15,
    };

    pub const Error = error{
        OutOfMemory,
        InitAudioBackend,
        SystemResources,
        OpeningDevice,
        NoSuchDevice,
        BackendUnavailable,
        Streaming,
        IncompatibleDevice,
        NoSuchClient,
        IncompatibleBackend,
        BackendDisconnected,
        Interrupted,
        Underflow,
        EncodingString,
    };

    fn wrapError(err: CError) Error!void {
        switch (err) {
            .None => return,
            .NoMem => return error.OutOfMemory,
            .InitAudioBackend => return error.InitAudioBackend,
            .SystemResources => return error.SystemResources,
            .OpeningDevice => return error.OpeningDevice,
            .NoSuchDevice => return error.NoSuchDevice,
            .Invalid => unreachable,
            .BackendUnavailable => return error.BackendUnavailable,
            .Streaming => return error.Streaming,
            .IncompatibleDevice => return error.IncompatibleDevice,
            .NoSuchClient => return error.NoSuchClient,
            .IncompatibleBackend => return error.IncompatibleBackend,
            .BackendDisconnected => return error.BackendDisconnected,
            .Interrupted => return error.Interrupted,
            .Underflow => return error.Underflow,
            .EncodingString => return error.EncodingString,
        }
    }

    pub const Backend = enum(c_int) {
        None = 0,
        Jack = 1,
        PulseAudio = 2,
        Alsa = 3,
        CoreAudio = 4,
        Wasapi = 5,
        Dummy = 6,
    };

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

    pub const Device = extern struct {
        soundio: *SoundIo,
        id: [*:0]const u8,
        name: [*:0]const u8,
        aim: Aim,
        layouts: [*]ChannelLayout,
        layout_count: c_int,
        current_layout: ChannelLayout,
        formats: [*]Format,
        format_count: c_int,
        current_format: Format,
        sample_rates: [*]SampleRateRange,
        sample_rate_count: c_int,
        sample_rate_current: c_int,
        software_latency_min: f64,
        software_latency_max: f64,
        software_latency_current: f64,
        is_raw: bool,
        ref_count: c_int,
        probe_error: c_int,

        pub const Aim = enum(c_int) { Input, Output };
    };

    pub const SampleRateRange = extern struct {
        min: c_int,
        max: c_int,
    };

    pub const OutStream = extern struct {
        device: [*c]Device,
        format: Format,
        sample_rate: c_int,
        layout: ChannelLayout,
        software_latency: f64,
        volume: f32,
        userdata: ?*anyopaque,
        write_callback: ?fn ([*c]OutStream, c_int, c_int) callconv(.C) void,
        underflow_callback: ?fn ([*c]OutStream) callconv(.C) void,
        error_callback: ?fn ([*c]OutStream, c_int) callconv(.C) void,
        name: [*c]const u8,
        non_terminal_hint: bool,
        bytes_per_frame: c_int,
        bytes_per_sample: c_int,
        layout_error: c_int,
    };

    pub const ChannelArea = extern struct {
        ptr: [*c]u8,
        step: c_int,
    };

    pub const InStream = extern struct {
        device: [*c]Device,
        format: Format,
        sample_rate: c_int,
        layout: ChannelLayout,
        software_latency: f64,
        userdata: ?*anyopaque,
        read_callback: ?fn ([*c]InStream, c_int, c_int) callconv(.C) void,
        overflow_callback: ?fn ([*c]InStream) callconv(.C) void,
        error_callback: ?fn ([*c]InStream, c_int) callconv(.C) void,
        name: [*c]const u8,
        non_terminal_hint: bool,
        bytes_per_frame: c_int,
        bytes_per_sample: c_int,
        layout_error: c_int,
    };
};
