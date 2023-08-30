const std = @import("std");
const Allocator = std.mem.Allocator;
const json = std.json;
const Tag = std.meta.Tag;

const keese = @import("keese.zig");

const TODO = struct {
    pub fn jsonParse(allocator: Allocator, source: anytype, options: json.ParseOptions) !@This() {
        _ = allocator;
        _ = source;
        _ = options;
        @panic("TODO");
    }
    pub fn jsonParseFromValue(allocator: Allocator, source: json.Value, options: json.ParseOptions) !@This() {
        _ = allocator;
        _ = source;
        _ = options;
        @panic("TODO");
    }
    pub fn jsonStringify(self: @This(), jw: anytype) !void {
        _ = self;
        _ = jw;
        @panic("TODO");
    }
};

pub const Datetime = struct {
    /// Milliseconds since UNIX Epoch.
    /// Actually, this can be milliseconds since any fixed arbitrary point in time,
    /// because of client-server clock skew correction.
    value: i64,
    /// Set this to test that clients are properly calibrating client-server clock skew.
    const skew_testing_offset = 123_000_000;
    pub fn jsonParse(allocator: Allocator, source: anytype, options: json.ParseOptions) !@This() {
        return .{ .value = skew_testing_offset - try json.innerParse(i64, allocator, source, options) };
    }
    pub fn jsonParseFromValue(allocator: Allocator, source: json.Value, options: json.ParseOptions) !@This() {
        return .{ .value = skew_testing_offset - try json.innerParseFromValue(i64, allocator, source, options) };
    }
    pub fn jsonStringify(self: @This(), jw: anytype) !void {
        return jw.write(skew_testing_offset + self.value);
    }
};

pub const Id = struct {
    value: u48,

    pub fn random() @This() {
        var buf: [6]u8 = undefined;
        std.crypto.random.bytes(&buf);
        return .{ .value = std.mem.readIntNative(u48, &buf) };
    }

    pub fn parse(s: []const u8) !@This() {
        if (s.len != 8) return error.InvalidUuidLen;
        var buf: [6]u8 = undefined;
        try std.base64.url_safe.Decoder.decode(&buf, s);
        return .{ .value = std.mem.readIntNative(u48, &buf) };
    }
    pub fn write(self: @This(), out_buffer: *[8]u8) []const u8 {
        var native: [6]u8 = undefined;
        std.mem.writeIntNative(u48, &native, self.value);
        std.debug.assert(std.base64.url_safe.Encoder.encode(out_buffer, &native).len == 8);
        return out_buffer;
    }

    // JSON interface
    pub fn jsonParse(allocator: Allocator, source: anytype, options: json.ParseOptions) !@This() {
        _ = options;
        switch (try source.nextAlloc(.alloc_if_needed)) {
            .string => |s| return parse(s) catch return error.UnexpectedToken,
            .allocated_string => |s| {
                defer allocator.free(s);
                return parse(s) catch return error.UnexpectedToken;
            },
            else => return error.UnexpectedToken,
        }
    }
    pub fn jsonParseFromValue(allocator: Allocator, source: json.Value, options: json.ParseOptions) !@This() {
        _ = allocator;
        _ = options;
        switch (source) {
            .string => |s| return parse(s) catch return error.UnexpectedToken,
            else => return error.UnexpectedToken,
        }
    }
    pub fn jsonStringify(self: @This(), jw: anytype) !void {
        var buf: [8]u8 = undefined;
        try jw.write(self.write(&buf));
    }

    // std.fmt interface
    pub fn format(self: @This(), comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;
        var buf: [8]u8 = undefined;
        return writer.writeAll(self.write(&buf));
    }
};

pub const IdOrGuest = union(enum) {
    id: Id,
    guest,

    const guest_pseudo_id = "(guest)";

    // JsonMap interface.
    pub fn parse(s: []const u8) !@This() {
        if (std.mem.eql(u8, s, guest_pseudo_id)) return .guest;
        return .{ .id = try Id.parse(s) };
    }
    pub fn write(self: @This(), buf: *[8]u8) []const u8 {
        switch (self) {
            .id => |id| return id.write(buf),
            .guest => return guest_pseudo_id,
        }
    }

    // JSON interface
    pub fn jsonParse(allocator: Allocator, source: anytype, options: json.ParseOptions) !@This() {
        _ = options;
        switch (try source.nextAlloc(.alloc_if_needed)) {
            .string => |s| return parse(s) catch return error.UnexpectedToken,
            .allocated_string => |s| {
                defer allocator.free(s);
                return parse(s) catch return error.UnexpectedToken;
            },
            else => return error.UnexpectedToken,
        }
    }
    pub fn jsonParseFromValue(allocator: Allocator, source: json.Value, options: json.ParseOptions) !@This() {
        _ = allocator;
        _ = options;
        switch (source) {
            .string => |s| return parse(s) catch return error.UnexpectedToken,
            else => return error.UnexpectedToken,
        }
    }
    pub fn jsonStringify(self: @This(), jw: anytype) !void {
        var buf: [8]u8 = undefined;
        try jw.write(self.write(&buf));
    }

    // std.fmt interface
    pub fn format(self: @This(), comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;
        var buf: [8]u8 = undefined;
        return writer.writeAll(self.write(&buf));
    }
};

pub const EventUserId = union(enum) {
    user: Id,
    system,
    deleted_user,

    const deleted_pseudo_id = "(del)";

    // JSON interface
    pub fn jsonStringify(self: @This(), jw: anytype) !void {
        switch (self) {
            .user => |id| return id.jsonStringify(jw),
            .system => return jw.write(null),
            .deleted_user => return jw.write(deleted_pseudo_id),
        }
    }

    // std.fmt interface
    pub fn format(self: @This(), comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        switch (self) {
            .user => |id| return id.format(fmt, options, writer),
            .system => return writer.writeAll("null"),
            .deleted_user => return writer.writeAll(deleted_pseudo_id),
        }
    }
};

pub fn IdMap(comptime T: type) type {
    return JsonMap(Id, T);
}
pub fn IdOrGuestMap(comptime T: type) type {
    return JsonMap(IdOrGuest, T);
}
fn JsonMap(comptime Key: type, comptime T: type) type {
    // Adapted from std.json.ArrayHashMap.
    return struct {
        map: Map = .{},

        pub const Map = std.AutoArrayHashMapUnmanaged(Key, T);

        pub fn deinit(self: *@This(), allocator: Allocator) void {
            self.map.deinit(allocator);
        }

        pub fn jsonParse(allocator: Allocator, source: anytype, options: json.ParseOptions) !@This() {
            var map = Map{};
            errdefer map.deinit(allocator);

            if (.object_begin != try source.next()) return error.UnexpectedToken;
            while (true) {
                const token = try source.nextAlloc(allocator, options.allocate.?);
                switch (token) {
                    inline .string, .allocated_string => |s| {
                        const id = Key.parse(s) catch return error.UnexpectedToken;
                        const gop = try map.getOrPut(allocator, id);
                        if (gop.found_existing) {
                            switch (options.duplicate_field_behavior) {
                                .use_first => {
                                    // Parse and ignore the redundant value.
                                    // We don't want to skip the value, because we want type checking.
                                    _ = try json.innerParse(T, allocator, source, options);
                                    continue;
                                },
                                .@"error" => return error.DuplicateField,
                                .use_last => {},
                            }
                        }
                        gop.value_ptr.* = try json.innerParse(T, allocator, source, options);
                    },
                    .object_end => break,
                    else => unreachable,
                }
            }
            return .{ .map = map };
        }

        pub fn jsonParseFromValue(allocator: Allocator, source: json.Value, options: json.ParseOptions) !@This() {
            if (source != .object) return error.UnexpectedToken;

            var map = Map{};
            errdefer map.deinit(allocator);

            var it = source.object.iterator();
            while (it.next()) |kv| {
                const id = Key.parse(kv.key_ptr.*) catch return error.UnexpectedToken;
                try map.put(allocator, id, try json.innerParseFromValue(T, allocator, kv.value_ptr.*, options));
            }
            return .{ .map = map };
        }

        pub fn jsonStringify(self: @This(), jws: anytype) !void {
            try jws.beginObject();
            var it = self.map.iterator();
            while (it.next()) |kv| {
                var buf: [8]u8 = undefined;
                try jws.objectField(kv.key_ptr.*.write(&buf));
                try jws.write(kv.value_ptr.*);
            }
            try jws.endObject();
        }
    };
}

fn nameAndArgsParse(comptime T: type, allocator: Allocator, source: anytype, options: json.ParseOptions) !T {
    // The fields can appear in any order, and we need to know the value of one before we can parse the other.
    var dynamic_value = try json.parseFromTokenSourceLeaky(json.Value, allocator, source, options);
    return nameAndArgsParseFromValue(T, allocator, dynamic_value, options);
}
fn nameAndArgsParseFromValue(comptime T: type, allocator: Allocator, source: json.Value, options: json.ParseOptions) !T {
    if (source != .object) return error.UnexpectedToken;
    const tag_name = switch (source.object.get("name") orelse return error.MissingField) {
        .string => |s| s,
        else => return error.UnexpectedToken,
    };
    inline for (@typeInfo(T).Union.fields) |u_field| {
        if (std.mem.eql(u8, u_field.name, tag_name)) {
            if (u_field.type == void) {
                // Ignore void "args".
                return @unionInit(T, u_field.name, {});
            }
            const value = source.object.get("args") orelse return error.MissingField;
            return @unionInit(T, u_field.name, try json.innerParseFromValue(u_field.type, allocator, value, options));
        }
    } else return error.UnknownField;
}
fn nameAndArgsStringify(self: anytype, jw: anytype) !void {
    const tag_name = @tagName(self);

    try jw.beginObject();

    try jw.objectField("name");
    try jw.write(tag_name);

    inline for (@typeInfo(@TypeOf(self)).Union.fields) |u_field| {
        if (std.mem.eql(u8, u_field.name, tag_name)) {
            if (u_field.type == void) {
                // Omit void "args".
                break;
            }
            try jw.objectField("args");
            try jw.write(@field(self, u_field.name));
            break;
        }
    } else unreachable;

    try jw.endObject();
}

// Client-to-Server Control Messages
pub const ClientToServerMessage = union(enum) {
    approve: []const struct {
        id: Id,
        replaceId: ?Id,
        approved: bool,
        name: []const u8,
    },
    chat: struct {
        text: []const u8,
        displayClass: ?enum { me } = null,
    },
    deleteTracks: TODO,
    deleteUsers: []const Id,
    autoDjOn: TODO,
    autoDjHistorySize: TODO,
    autoDjFutureSize: TODO,
    ensureAdminUser: void,
    hardwarePlayback: TODO,
    importNames: TODO,
    importUrl: TODO,
    login: struct {
        username: []const u8,
        password: []u8,
    },
    logout: void,
    subscribe: struct {
        name: Tag(Subscription),
        delta: bool = false,
        version: ?Id = null,
    },
    updateTags: TODO,
    updateUser: struct {
        userId: IdOrGuest,
        perms: Permissions,
    },
    unsubscribe: TODO,
    move: IdMap(struct {
        sortKey: keese.Value,
    }),
    pause: void,
    play: void,
    queue: IdMap(struct {
        key: Id,
        sortKey: keese.Value,
    }),
    seek: struct {
        id: Id,
        pos: f64,
    },
    setStreaming: bool,
    remove: []Id,
    repeat: TODO,
    requestApproval: void,
    setVolume: TODO,
    stop: TODO,
    playlistCreate: TODO,
    playlistRename: TODO,
    playlistDelete: TODO,
    playlistAddItems: TODO,
    playlistRemoveItems: TODO,
    playlistMoveItems: TODO,
    labelCreate: TODO,
    labelRename: TODO,
    labelColorUpdate: TODO,
    labelDelete: TODO,
    labelAdd: TODO,
    labelRemove: TODO,
    lastFmGetSession: TODO,
    lastFmScrobblersAdd: TODO,
    lastFmScrobblersRemove: TODO,

    pub fn jsonParse(allocator: Allocator, source: anytype, options: json.ParseOptions) !@This() {
        return nameAndArgsParse(@This(), allocator, source, options);
    }
    pub fn jsonParseFromValue(allocator: Allocator, source: json.Value, options: json.ParseOptions) !@This() {
        return nameAndArgsParseFromValue(@This(), allocator, source, options);
    }
    pub fn jsonStringify(self: @This(), jw: anytype) !void {
        return nameAndArgsStringify(self, jw);
    }
};

pub const ServerToClientMessage = union(enum) {
    // Server-to-Client Control Messages
    @"error": TODO,
    seek: TODO,
    time: Datetime,
    token: TODO,
    lastFmApiKey: TODO,
    lastFmGetSessionSuccess: TODO,
    lastFmGetSessionError: TODO,
    sessionId: Id,

    // Subscribed Information Change Messages
    subscription: struct {
        sub: Subscription,
        // (this is not the JSON structure. see jsonStringify below.)
        delta_version: ?Id = null,
    },

    pub fn jsonParse(allocator: Allocator, source: anytype, options: json.ParseOptions) !@This() {
        return nameAndArgsParse(@This(), allocator, source, options);
    }
    pub fn jsonParseFromValue(allocator: Allocator, source: json.Value, options: json.ParseOptions) !@This() {
        return nameAndArgsParseFromValue(@This(), allocator, source, options);
    }
    pub fn jsonStringify(self: @This(), jw: anytype) !void {
        switch (self) {
            .subscription => |data| {
                const tag_name = @tagName(data.sub);

                try jw.beginObject();
                try jw.objectField("name");
                try jw.write(tag_name);
                try jw.objectField("args");

                // Conditionally use delta format.
                if (data.delta_version) |v| {
                    try jw.beginObject();
                    try jw.objectField("version");
                    try jw.write(v);
                    try jw.objectField("reset");
                    try jw.write(true);
                    try jw.objectField("delta");
                }

                inline for (@typeInfo(Subscription).Union.fields) |u_field| {
                    if (std.mem.eql(u8, u_field.name, tag_name)) {
                        try jw.write(@field(data.sub, u_field.name));
                        break;
                    }
                } else unreachable;

                if (data.delta_version) |_| {
                    try jw.endObject();
                }
                try jw.endObject();
            },
            else => return nameAndArgsStringify(self, jw),
        }
    }
};

pub const LibraryTrack = struct {
    /// (undocumented) The id in the library.
    key: Id,
    /// Path of the song on disk relative to the music library root.
    file: []const u8 = "",
    /// How many seconds long this track is.
    /// Once the track has been scanned for loudness, this duration value is always exactly correct.
    duration: f64 = 0,
    /// Track title.
    name: []const u8 = "",
    artistName: []const u8 = "",
    albumArtistName: []const u8 = "",
    albumName: []const u8 = "",
    compilation: bool = false,
    /// Which track number this is.
    track: i16 = 0,
    /// How many total tracks there are on this album.
    trackCount: i16 = 0,
    /// Which disc number this is.
    disc: i16 = 0,
    /// How many total discs there are in this compilation.
    discCount: i16 = 0,
    /// What year this track was released.
    year: i16 = 0,
    genre: []const u8 = "",
    composerName: []const u8 = "",
    performerName: []const u8 = "",
    /// The ids of labels that apply to this song. The values are always 1.
    labels: IdMap(AlwaysTheNumber1) = .{},
};

pub const QueueItem = struct {
    key: Id,
    sortKey: keese.Value,
    isRandom: bool,
};

pub const Session = struct {
    userId: Id,
    streaming: bool,
};
pub const PublicUserInfo = struct {
    name: []const u8,
    perms: Permissions,
    registration: enum {
        guest,
        named_by_user,
        requested_approval,
        approved,
    },
};

pub const Event = struct {
    date: Datetime,
    type: enum {
        chat,
    },
    sortKey: ?keese.Value = null,
    userId: EventUserId = .system,
    text: ?[]const u8 = null,
    trackId: ?TODO = null,
    pos: ?TODO = null,
    displayClass: ?enum { me } = null,
    playlistId: ?TODO = null,
};

pub const CurrentTrack = struct {
    /// `string` or `null`. The play queue ID currently playing.
    currentItemId: ?Id,
    /// `boolean`. `true` if playing; `false` if paused.
    isPlaying: bool,
    /// datetime representing what time it was on the server when frame 0 of
    /// the current song was played.
    trackStartDate: ?Datetime,
    /// `number`. Only relevant when `isPlaying` is `false`. How many seconds
    /// into the song the position is.
    pausedTime: ?f64,
};

pub const Subscription = union(enum) {
    sessions: IdMap(Session),
    users: IdOrGuestMap(PublicUserInfo),
    currentTrack: CurrentTrack,
    autoDjOn: TODO,
    autoDjHistorySize: TODO,
    autoDjFutureSize: TODO,
    repeat: TODO,
    volume: TODO,
    queue: IdMap(QueueItem),
    hardwarePlayback: TODO,
    library: IdMap(LibraryTrack),
    libraryQueue: TODO,
    scanning: TODO,
    playlists: TODO,
    importProgress: TODO,
    anonStreamers: TODO,
    streamEndpoint: []const u8,
    protocolMetadata: TODO,
    labels: TODO, // undocumented.
    events: IdMap(Event),
};

const AlwaysTheNumber1 = struct {
    pub fn jsonParse(allocator: Allocator, source: anytype, options: json.ParseOptions) !@This() {
        _ = allocator;
        _ = options;
        // When reading this type, we don't care what value is given to us.
        try source.skipValue();
    }
    pub fn jsonParseFromValue(allocator: Allocator, source: json.Value, options: json.ParseOptions) !@This() {
        _ = allocator;
        _ = source;
        _ = options;
    }
    pub fn jsonStringify(self: @This(), jw: anytype) !void {
        _ = self;
        try jw.write(1);
    }
};

pub const Permissions = packed struct {
    read: bool,
    add: bool,
    control: bool,
    playlist: bool,
    admin: bool,
};

pub const default_permissions = Permissions{
    .read = true,
    .add = true,
    .control = true,
    .playlist = false,
    .admin = false,
};
