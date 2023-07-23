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
