const std = @import("std");
const log = std.log;
const mem = std.mem;
const Allocator = std.mem.Allocator;

/// milliseconds since posix epoch
const Timestamp = enum(u64) { _ };

/// milliseconds since start of track
const SeekPosition = enum(u32) { _ };

const NullTerminatedString = enum(u32) { _ };
const OptionalNullTerminatedString = enum(u32) { _ };

/// An event's ID is its index in the events array.
/// Events are sorted by ID since all events are server-generated.
const Event = struct {
    /// milliseconds since posix epoch.
    timestamp: Timestamp,

    const Tag = enum(u8) {
        queue,
        connect,
        seek,
        current_track,
        auto_pause,
        part,
        stream_stop,
        stream_start,
        play,
        pause,
        remove,
        login,
        chat,
        move,
        label_add,
    };

    const Queue = struct {
        user_id: u32,
        track_id: u32,
        /// number of queued tracks
        n: u32,
    };

    const Connect = struct {
        user_id: u32,
    };

    const Seek = struct {
        user_id: u32,
        track_id: u32,
        pos: SeekPosition,
    };

    const CurrentTrack = struct {
        track_id: u32,
        /// looks like "The Greener Grass - Fair To Midland - Arrows & Anchors"
        text: NullTerminatedString,
    };

    const Part = struct {
        user_id: u32,
    };

    const StreamStop = struct {
        user_id: u32,
    };

    const StreamStart = struct {
        user_id: u32,
    };

    const Play = struct {
        user_id: u32,
    };

    const Pause = struct {
        user_id: u32,
    };

    const Remove = struct {
        user_id: u32,
        /// If only one item removed, "now playing text" of the item. Might be
        /// useful for if the song is later deleted from the library.
        text: NullTerminatedString,
        /// number of items removed
        n: u32,
    };

    const Login = struct {
        user_id: u32,
    };

    const Chat = struct {
        user_id: u32,
        text: NullTerminatedString,
    };

    const Move = struct {
        user_id: u32,
    };

    const LabelAdd = struct {
        user_id: u32,
        label_id: u32,
        track_id: u32,
        label_count: u32,
        track_count: u32,
    };
};

const Track = struct {
    name: NullTerminatedString,
    artist_name: OptionalNullTerminatedString,
    album_artist_name: OptionalNullTerminatedString,
    album_name: OptionalNullTerminatedString,
    composer_name: OptionalNullTerminatedString,
    performer_name: OptionalNullTerminatedString,
    genre: OptionalNullTerminatedString,
    file: NullTerminatedString,
    last_queue_date: Timestamp,
    compilation: bool,
    track: i16,
    track_count: i16,
    disc: i16,
    disc_count: i16,
    duration: SeekPosition,
    year: i32,
    mtime: i128,
    replay_gain_album_gain: f32,
    replay_gain_album_peak: f32,
    replay_gain_track_gain: f32,
    replay_gain_track_peak: f32,
    /// TODO
    fingerprint: FingerprintIndex,
    play_count: u32,
    /// TODO
    labels: LabelIndex,
};

const FingerprintIndex = enum(u32) {
    none = std.math.maxInt(u32),
    _,
};

const LabelIndex = enum(u32) {
    none = std.math.maxInt(u32),
    _,
};

/// A label's ID is its index in the labels array.
const Label = struct {
    name: NullTerminatedString,
    color: [3]u8,
};

const Scalars = struct {
    flags: Flags,
    current_track_id: u32,
    current_track_pos: SeekPosition,

    const Flags = packed struct {
        auto_dj_on: bool,
        hardware_playback: bool,
        repeat: enum(u2) { off, one, all },
        ///   0:   0%
        /// 255: 100%
        volume: u8,
    };
};

const Db = struct {
    string_bytes: std.ArrayListUnmanaged(u8) = .{},
    string_table: std.HashMapUnmanaged(
        u32,
        void,
        std.hash_map.StringIndexContext,
        std.hash_map.default_max_load_percentage,
    ) = .{},
    /// A bunch of flattened arrays, each prefixed with the length.
    fingerprint_integers: std.ArrayListUnmanaged(u32) = .{},

    arena: std.mem.Allocator,

    fn getOrPutString(
        db: *Db,
        gpa: Allocator,
        s: []const u8,
    ) Allocator.Error!NullTerminatedString {
        try db.string_bytes.ensureUnusedCapacity(gpa, s.len + 1);
        db.string_bytes.appendSliceAssumeCapacity(s);
        db.string_bytes.appendAssumeCapacity(0);
        return db.getOrPutTrailingString(gpa, s.len + 1);
    }

    /// Uses the last len bytes of db.string_bytes as the key.
    fn getOrPutTrailingString(
        db: *Db,
        gpa: Allocator,
        len: usize,
    ) Allocator.Error!NullTerminatedString {
        const string_bytes = &db.string_bytes;
        const str_index: u32 = @intCast(string_bytes.items.len - len);
        if (len > 0 and string_bytes.getLast() == 0) {
            _ = string_bytes.pop();
        } else {
            try string_bytes.ensureUnusedCapacity(gpa, 1);
        }
        const key: []const u8 = string_bytes.items[str_index..];
        const gop = try db.string_table.getOrPutContextAdapted(gpa, key, std.hash_map.StringIndexAdapter{
            .bytes = string_bytes,
        }, std.hash_map.StringIndexContext{
            .bytes = string_bytes,
        });
        if (gop.found_existing) {
            string_bytes.shrinkRetainingCapacity(str_index);
            return @enumFromInt(gop.key_ptr.*);
        } else {
            gop.key_ptr.* = str_index;
            string_bytes.appendAssumeCapacity(0);
            return @enumFromInt(str_index);
        }
    }
};

pub fn main() !void {
    var arena_instance = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    const arena = arena_instance.allocator();

    const args = try std.process.argsAlloc(arena);
    const input_json_file_name = args[1];

    const bytes = try std.fs.cwd().readFileAlloc(arena, input_json_file_name, 1_000_000_000);
    const json = try std.json.parseFromSliceLeaky(std.json.Value, arena, bytes, .{});

    var db: Db = .{
        .arena = arena,
    };

    var events_bytesize: usize = 0;
    var events_len: usize = 0;

    var tracks_len: usize = 0;
    var tracks_bytesize: usize = 0;

    var labels_len: usize = 0;
    var labels_bytesize: usize = 0;

    for (json.object.keys(), json.object.values()) |root_key, root_value| {
        if (mem.startsWith(u8, root_key, "Events.")) {
            events_len += 1;
            events_bytesize += @sizeOf(Event.Tag) + @sizeOf(Timestamp);
            var event_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            const event_type_name = event_json.object.fetchSwapRemove("type").?.value.string;
            // An event's ID is its index in the events array.
            _ = event_json.object.fetchSwapRemove("id").?.value.string;
            // Events are sorted by ID since all events are server-generated.
            _ = event_json.object.fetchSwapRemove("sortKey").?.value.string;
            // timestamp: u64
            _ = event_json.object.fetchSwapRemove("date").?.value.string;

            if (mem.eql(u8, event_type_name, "queue")) {
                events_bytesize += @sizeOf(Event.Queue);
                _ = event_json.object.fetchSwapRemove("trackId").?.value.string;
                _ = event_json.object.fetchSwapRemove("pos").?.value.integer;
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "connect")) {
                events_bytesize += @sizeOf(Event.Connect);
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "seek")) {
                events_bytesize += @sizeOf(Event.Seek);
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
                _ = event_json.object.fetchSwapRemove("trackId").?.value.string;
                _ = event_json.object.fetchSwapRemove("pos").?.value; // float or integer
            } else if (mem.eql(u8, event_type_name, "currentTrack")) {
                events_bytesize += @sizeOf(Event.CurrentTrack);
                _ = event_json.object.fetchSwapRemove("trackId").?.value.string;
                addString(&db, event_json.object.fetchSwapRemove("text").?.value.string);
            } else if (mem.eql(u8, event_type_name, "autoPause")) {
                events_bytesize += 0;
            } else if (mem.eql(u8, event_type_name, "part")) {
                events_bytesize += @sizeOf(Event.Part);
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "streamStop")) {
                events_bytesize += @sizeOf(Event.StreamStop);
                _ = event_json.object.fetchSwapRemove("userId");
            } else if (mem.eql(u8, event_type_name, "streamStart")) {
                events_bytesize += @sizeOf(Event.StreamStart);
                _ = event_json.object.fetchSwapRemove("userId");
            } else if (mem.eql(u8, event_type_name, "play")) {
                events_bytesize += @sizeOf(Event.Play);
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "pause")) {
                events_bytesize += @sizeOf(Event.Pause);
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "remove")) {
                events_bytesize += @sizeOf(Event.Remove);
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
                switch (event_json.object.fetchSwapRemove("text").?.value) {
                    .string => |s| addString(&db, s),
                    .bool => {}, // unclear why this ever happens
                    else => unreachable,
                }
                switch (event_json.object.fetchSwapRemove("trackId").?.value) {
                    .string => {},
                    .bool => {}, // unclear why this ever happens
                    else => unreachable,
                }
                // number of items removed
                _ = event_json.object.fetchSwapRemove("pos").?.value.integer;
            } else if (mem.eql(u8, event_type_name, "login")) {
                events_bytesize += @sizeOf(Event.Login);
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "chat")) {
                events_bytesize += @sizeOf(Event.Chat);
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
                addString(&db, event_json.object.fetchSwapRemove("text").?.value.string);
                // absorb into the event enum tag
                _ = event_json.object.fetchSwapRemove("displayClass").?.value;
            } else if (mem.eql(u8, event_type_name, "move")) {
                events_bytesize += @sizeOf(Event.Move);
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "labelAdd")) {
                events_bytesize += @sizeOf(Event.LabelAdd);
                _ = event_json.object.fetchSwapRemove("labelId").?.value.string;
                _ = event_json.object.fetchSwapRemove("trackId").?.value.string;
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
                _ = event_json.object.fetchSwapRemove("subCount").?.value.integer;
                _ = event_json.object.fetchSwapRemove("pos").?.value.integer;
            } else {
                log.err("unknown event type '{s}'", .{event_type_name});
            }

            for (event_json.object.keys()) |k| {
                log.err("unknown key for event type '{s}': '{s}'", .{ event_type_name, k });
            }
        } else if (mem.startsWith(u8, root_key, "Label.")) {
            labels_len += 1;
            labels_bytesize += @sizeOf(Label);
            var label_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            // A label's ID is its index in the labels array.
            _ = label_json.object.fetchSwapRemove("id").?.value.string;
            addString(&db, label_json.object.fetchSwapRemove("name").?.value.string);
            _ = label_json.object.fetchSwapRemove("color").?.value.string;

            for (label_json.object.keys()) |k| {
                log.err("unknown key for label '{s}': '{s}'", .{ root_key, k });
            }
        } else if (mem.startsWith(u8, root_key, "Library.")) {
            tracks_len += 1;
            tracks_bytesize += @sizeOf(Track);
            var track_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            // A track's ID is its index in the tracks array.
            addString(&db, track_json.object.fetchSwapRemove("key").?.value.string);
            addString(&db, track_json.object.fetchSwapRemove("name").?.value.string);
            addString(&db, track_json.object.fetchSwapRemove("artistName").?.value.string);
            addString(&db, track_json.object.fetchSwapRemove("albumArtistName").?.value.string);
            addString(&db, track_json.object.fetchSwapRemove("albumName").?.value.string);
            addString(&db, track_json.object.fetchSwapRemove("composerName").?.value.string);
            addString(&db, track_json.object.fetchSwapRemove("performerName").?.value.string);
            _ = track_json.object.fetchSwapRemove("lastQueueDate").?.value.string;
            _ = track_json.object.fetchSwapRemove("compilation").?.value.bool;
            _ = track_json.object.fetchSwapRemove("track"); // integer or missing
            _ = track_json.object.fetchSwapRemove("trackCount"); // integer or missing
            _ = track_json.object.fetchSwapRemove("disc"); // integer or missing
            _ = track_json.object.fetchSwapRemove("discCount"); // integer or missing
            _ = track_json.object.fetchSwapRemove("duration").?; // integer or float seconds
            _ = track_json.object.fetchSwapRemove("year"); // integer or missing
            if (track_json.object.fetchSwapRemove("genre")) |kv| {
                addString(&db, kv.value.string);
            }
            addString(&db, track_json.object.fetchSwapRemove("file").?.value.string);
            _ = track_json.object.fetchSwapRemove("mtime").?.value.integer;
            _ = track_json.object.fetchSwapRemove("replayGainAlbumGain"); // float, int, or missing
            _ = track_json.object.fetchSwapRemove("replayGainAlbumPeak"); // float, int, or missing
            _ = track_json.object.fetchSwapRemove("replayGainTrackGain"); // float, int, or missing
            _ = track_json.object.fetchSwapRemove("replayGainTrackPeak"); // float, int, or missing
            // it's an array of signed integers, or missing
            if (track_json.object.fetchSwapRemove("fingerprint")) |kv| {
                try db.fingerprint_integers.ensureUnusedCapacity(arena, kv.value.array.items.len + 1);
                db.fingerprint_integers.appendAssumeCapacity(@intCast(kv.value.array.items.len));

                for (kv.value.array.items) |fp_json_value| {
                    db.fingerprint_integers.appendAssumeCapacity(
                        @bitCast(@as(i32, @intCast(fp_json_value.integer))),
                    );
                }
            }
            _ = track_json.object.fetchSwapRemove("playCount"); // integer, null, or missing
            // it's map of label ids
            _ = track_json.object.fetchSwapRemove("labels").?.value.object;

            for (track_json.object.keys()) |k| {
                log.err("unknown key for track '{s}': '{s}'", .{ root_key, k });
            }
        } else if (mem.startsWith(u8, root_key, "LibraryDir.")) {
            var ldir_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            _ = ldir_json.object.fetchSwapRemove("dirName").?.value.string;
            _ = ldir_json.object.fetchSwapRemove("entries").?.value.object;
            _ = ldir_json.object.fetchSwapRemove("dirEntries").?.value.object;
            _ = ldir_json.object.fetchSwapRemove("mtime");

            for (ldir_json.object.keys()) |k| {
                log.err("unknown key for LibraryDir '{s}': '{s}'", .{ root_key, k });
            }
        } else if (mem.startsWith(u8, root_key, "Player.")) {
            // bytesize handled below with @sizeOf(Scalars)
            if (mem.eql(u8, root_key, "Player.autoDjOn")) {
                // bool
            } else if (mem.eql(u8, root_key, "Player.currentTrackInfo")) {
                // object. example:
                // "id": "jwRsBxnogVofMZbmmGgLqbtRHrf80NJ_",
                // "pos": 1.784
            } else if (mem.eql(u8, root_key, "Player.dynamicModeOn")) {
                // bool. deprecated name for "autoDjOn"
            } else if (mem.eql(u8, root_key, "Player.hardwarePlayback")) {
                // bool
            } else if (mem.eql(u8, root_key, "Player.repeat")) {
                // integer (enum with 3 states)
            } else if (mem.eql(u8, root_key, "Player.volume")) {
                // integer or float (0.0 - 1.0)
            } else {
                log.err("unknown Player key: '{s}'", .{root_key});
            }
        } else if (mem.startsWith(u8, root_key, "Playlist.")) {
            // This is the main play queue. Each of these are queue items.
            var pq_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            _ = pq_json.object.fetchSwapRemove("id").?.value.string;
            // the track_id to play
            _ = pq_json.object.fetchSwapRemove("key").?.value.string;
            _ = pq_json.object.fetchSwapRemove("sortKey").?.value.string;
            _ = pq_json.object.fetchSwapRemove("isRandom").?.value.bool;

            for (pq_json.object.keys()) |k| {
                log.err("unknown key for play queue item '{s}': '{s}'", .{ root_key, k });
            }
        } else if (mem.startsWith(u8, root_key, "Plugin.")) {
            if (mem.eql(u8, root_key, "Plugin.lastfm")) {
                // object that looks like this:
                // {
                // "scrobblers": {
                //     "username": "xxxxxxxxxxxxxxxxxxx-xxxxxxx-xxxx"
                // },
                // "scrobbles": []
                // }
            } else {
                log.err("unknown Plugin key: '{s}'", .{root_key});
            }
        } else if (mem.startsWith(u8, root_key, "StoredPlaylist.")) {
            // These are queue items in a playlist other than the main play queue.
            // `root_key` looks like this:
            // StoredPlaylist.3t9lhRClIGQZYCmGhO_fQiGfAU1wR5bJ.22TskRwXS4hG-oVhzNCvPRm439Q_SpHr
            // The first id is the playlist id and the second id is redundant with id below.
            var queue_item_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            _ = queue_item_json.object.fetchSwapRemove("id").?.value.string;
            // the track_id to play
            _ = queue_item_json.object.fetchSwapRemove("key").?.value.string;
            _ = queue_item_json.object.fetchSwapRemove("sortKey").?.value.string;

            for (queue_item_json.object.keys()) |k| {
                log.err("unknown key for play queue item '{s}': '{s}'", .{ root_key, k });
            }
        } else if (mem.startsWith(u8, root_key, "StoredPlaylistMeta.")) {
            // metadata of a playlist
            var pl_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            _ = pl_json.object.fetchSwapRemove("id").?.value.string;
            addString(&db, pl_json.object.fetchSwapRemove("name").?.value.string);
            _ = pl_json.object.fetchSwapRemove("mtime").?.value.integer;

            for (pl_json.object.keys()) |k| {
                log.err("unknown key for playlist item '{s}': '{s}'", .{ root_key, k });
            }
        } else if (mem.startsWith(u8, root_key, "Users.")) {
            var user_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            _ = user_json.object.fetchSwapRemove("id").?.value.string;
            addString(&db, user_json.object.fetchSwapRemove("name").?.value.string);
            _ = user_json.object.fetchSwapRemove("password").?.value.string;
            _ = user_json.object.fetchSwapRemove("registered").?.value.bool;
            _ = user_json.object.fetchSwapRemove("requested").?.value.bool;
            _ = user_json.object.fetchSwapRemove("approved").?.value.bool;
            // an object like this: {read, add, control, admin} all booleans
            _ = user_json.object.fetchSwapRemove("perms").?.value.object;

            for (user_json.object.keys()) |k| {
                log.err("unknown key for playlist item '{s}': '{s}'", .{ root_key, k });
            }
        } else {
            std.debug.panic("unrecognized key: '{s}'", .{root_key});
        }
    }

    const fingerprint_bytesize = db.fingerprint_integers.items.len * @sizeOf(u32);

    const total_bytesize = events_bytesize +
        tracks_bytesize +
        fingerprint_bytesize +
        labels_bytesize +
        @sizeOf(Scalars) +
        db.string_bytes.items.len;

    var bw = std.io.bufferedWriter(std.io.getStdOut().writer());
    const w = bw.writer();

    try w.print(
        \\Stats:
        \\
        \\     total events: {[events_len]}
        \\    total strings: {[strings_len]}
        \\     total tracks: {[tracks_len]}
        \\     total labels: {[labels_len]}
        \\ total label sets: TODO
        \\total queue items: TODO
        \\  total playlists: TODO
        \\      total users: TODO
        \\
        \\     labels bytes: {[labels_bytes]}
        \\  label set bytes: TODO
        \\ queue item bytes: TODO
        \\   playlist bytes: TODO
        \\      users bytes: TODO
        \\     events bytes: {[events_bytes]}
        \\    strings bytes: {[strings_bytes]}
        \\     tracks bytes: {[tracks_bytes]}
        \\fingerprint bytes: {[fingerprint_bytes]}
        \\
        \\      total bytes: {[total_bytes]}
        \\
    , .{
        .events_len = events_len,
        .strings_len = db.string_table.count(),
        .events_bytes = events_bytesize,
        .strings_bytes = db.string_bytes.items.len,
        .tracks_len = tracks_len,
        .tracks_bytes = tracks_bytesize,
        .labels_len = labels_len,
        .labels_bytes = labels_bytesize,
        .fingerprint_bytes = fingerprint_bytesize,
        .total_bytes = total_bytesize,
    });

    try bw.flush();
}

fn addString(db: *Db, s: []const u8) void {
    _ = db.getOrPutString(db.arena, s) catch @panic("OOM");
}
