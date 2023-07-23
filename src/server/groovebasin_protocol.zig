const std = @import("std");
const Allocator = std.mem.Allocator;
const json = std.json;
const JsonHashMap = json.ArrayHashMap;

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
    const value = source.object.get("args") orelse return error.MissingField;
    inline for (@typeInfo(T).Union.fields) |u_field| {
        if (std.mem.eql(u8, u_field.name, tag_name)) {
            return @unionInit(T, u_field.name, try json.innerParseFromValue(u_field.type, allocator, value, options));
        }
    } else return error.UnknownField;
}
fn nameAndArgsStringify(self: anytype, jw: anytype) !void {
    const tag_name = @tagName(self);

    try jw.beginObject();

    try jw.objectField("name");
    try jw.write(tag_name);

    try jw.objectField("args");
    inline for (@typeInfo(@TypeOf(self)).Union.fields) |u_field| {
        if (std.mem.eql(u8, u_field.name, tag_name)) {
            try jw.write(@field(self, u_field.name));
            break;
        }
    } else unreachable;

    try jw.endObject();
}

// Client-to-Server Control Messages
pub const ClientToServerMessage = union(enum) {
    approve: TODO,
    chat: TODO,
    deleteTracks: TODO,
    deleteUsers: TODO,
    autoDjOn: TODO,
    autoDjHistorySize: TODO,
    autoDjFutureSize: TODO,
    ensureAdminUser: TODO,
    hardwarePlayback: TODO,
    importNames: TODO,
    importUrl: TODO,
    login: TODO,
    logout: TODO,
    subscribe: struct {
        name: []const u8,
        delta: bool = false,
        version: ?[]const u8 = null,
    },
    updateTags: TODO,
    updateUser: TODO,
    unsubscribe: TODO,
    move: TODO,
    pause: TODO,
    play: TODO,
    queue: TODO,
    seek: TODO,
    setStreaming: bool,
    remove: TODO,
    repeat: TODO,
    requestApproval: TODO,
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
    time: TODO,
    token: TODO,
    lastFmApiKey: TODO,
    lastFmGetSessionSuccess: TODO,
    lastFmGetSessionError: TODO,
    user: TODO,
    // Subscribed Information Change Messages
    currentTrack: TODO,
    autoDjOn: TODO,
    autoDjHistorySize: TODO,
    autoDjFutureSize: TODO,
    repeat: TODO,
    volume: TODO,
    queue: TODO,
    hardwarePlayback: TODO,
    library: JsonHashMap(LibraryTrack),
    libraryQueue: TODO,
    scanning: TODO,
    playlists: TODO,
    importProgress: TODO,
    anonStreamers: TODO,
    haveAdminUser: TODO,
    users: TODO,
    streamEndpoint: TODO,
    protocolMetadata: TODO,
    events: TODO,

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

pub const LibraryTrack = struct {
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
    labels: JsonHashMap(AlwaysTheNumber1) = .{},
};

//chat
//queue
//currentTrack
//autoPause
//streamStart
//streamStop
//connect
//part
//register
//login
//move
//pause
//play
//stop
//seek
//playlistRename
//playlistDelete
//playlistCreate
//playlistAddItems
//playlistRemoveItems
//playlistMoveItems
//clearQueue
//remove
//shuffle
//import
//labelCreate
//labelRename
//labelColorUpdate
//labelDelete
//labelAdd
//labelRemove

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
