const std = @import("std");
const log = std.log;
const mem = std.mem;

/// An event's ID is its index in the events array.
/// Events are sorted by ID since all events are server-generated.
const Event = struct {
    /// milliseconds since posix epoch.
    timestamp: u64,

    const Part = struct {
        user_id: u32,
    };

    const Queue = struct {
        user_id: u32,
        track_id: u32,
        /// number of queued tracks
        n: u32,
    };
};

/// A label's ID is its index in the labels array.
const Label = struct {
    name: NullTerminatedStringIndex,
    color: [3]u8,
};

const NullTerminatedStringIndex = enum(u32) { _ };

pub fn main() !void {
    var arena_instance = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    const arena = arena_instance.allocator();

    const args = try std.process.argsAlloc(arena);
    const input_json_file_name = args[1];

    const bytes = try std.fs.cwd().readFileAlloc(arena, input_json_file_name, 1_000_000_000);
    const json = try std.json.parseFromSliceLeaky(std.json.Value, arena, bytes, .{});

    for (json.object.keys(), json.object.values()) |root_key, root_value| {
        if (mem.startsWith(u8, root_key, "Events.")) {
            var event_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            const event_type_name = event_json.object.fetchSwapRemove("type").?.value.string;
            // An event's ID is its index in the events array.
            _ = event_json.object.fetchSwapRemove("id").?.value.string;
            // Events are sorted by ID since all events are server-generated.
            _ = event_json.object.fetchSwapRemove("sortKey").?.value.string;
            // timestamp: u64
            _ = event_json.object.fetchSwapRemove("date").?.value.string;

            if (mem.eql(u8, event_type_name, "queue")) {
                _ = event_json.object.fetchSwapRemove("trackId").?.value.string;
                _ = event_json.object.fetchSwapRemove("pos").?.value.integer;
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "connect")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "seek")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
                _ = event_json.object.fetchSwapRemove("trackId").?.value.string;
                _ = event_json.object.fetchSwapRemove("pos").?.value; // float or integer
            } else if (mem.eql(u8, event_type_name, "currentTrack")) {
                _ = event_json.object.fetchSwapRemove("trackId").?.value.string;
                _ = event_json.object.fetchSwapRemove("text").?.value.string;
            } else if (mem.eql(u8, event_type_name, "autoPause")) {
                //
            } else if (mem.eql(u8, event_type_name, "part")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "streamStop")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "streamStart")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "play")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "pause")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "remove")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
                _ = event_json.object.fetchSwapRemove("text").?.value; // should be string but observed to be a bool sometimes (?)
                switch (event_json.object.fetchSwapRemove("trackId").?.value) {
                    .string => {},
                    .bool => {}, // unclear why this ever happens
                    else => unreachable,
                }
                // number of items removed
                _ = event_json.object.fetchSwapRemove("pos").?.value.integer;
            } else if (mem.eql(u8, event_type_name, "login")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "chat")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
                _ = event_json.object.fetchSwapRemove("text").?.value.string;
                _ = event_json.object.fetchSwapRemove("displayClass").?.value;
            } else if (mem.eql(u8, event_type_name, "move")) {
                _ = event_json.object.fetchSwapRemove("userId").?.value.string;
            } else if (mem.eql(u8, event_type_name, "labelAdd")) {
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
            var label_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            // A label's ID is its index in the labels array.
            _ = label_json.object.fetchSwapRemove("id").?.value.string;
            _ = label_json.object.fetchSwapRemove("name").?.value.string;
            _ = label_json.object.fetchSwapRemove("color").?.value.string;

            for (label_json.object.keys()) |k| {
                log.err("unknown key for label '{s}': '{s}'", .{ root_key, k });
            }
        } else if (mem.startsWith(u8, root_key, "Library.")) {
            var track_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            // A track's ID is its index in the tracks array.
            _ = track_json.object.fetchSwapRemove("key").?.value.string;
            _ = track_json.object.fetchSwapRemove("name").?.value.string;
            _ = track_json.object.fetchSwapRemove("artistName").?.value.string;
            _ = track_json.object.fetchSwapRemove("albumArtistName").?.value.string;
            _ = track_json.object.fetchSwapRemove("albumName").?.value.string;
            _ = track_json.object.fetchSwapRemove("composerName").?.value.string;
            _ = track_json.object.fetchSwapRemove("performerName").?.value.string;
            _ = track_json.object.fetchSwapRemove("lastQueueDate").?.value.string;
            _ = track_json.object.fetchSwapRemove("compilation").?.value.bool;
            _ = track_json.object.fetchSwapRemove("track"); // integer or missing
            _ = track_json.object.fetchSwapRemove("trackCount"); // integer or missing
            _ = track_json.object.fetchSwapRemove("disc"); // integer or missing
            _ = track_json.object.fetchSwapRemove("discCount"); // integer or missing
            _ = track_json.object.fetchSwapRemove("duration").?; // integer or float seconds
            _ = track_json.object.fetchSwapRemove("year"); // integer or missing
            _ = track_json.object.fetchSwapRemove("genre"); // string or missing
            _ = track_json.object.fetchSwapRemove("file").?.value.string;
            _ = track_json.object.fetchSwapRemove("mtime").?.value.integer;
            _ = track_json.object.fetchSwapRemove("replayGainAlbumGain"); // float, int, or missing
            _ = track_json.object.fetchSwapRemove("replayGainAlbumPeak"); // float, int, or missing
            _ = track_json.object.fetchSwapRemove("replayGainTrackGain"); // float, int, or missing
            _ = track_json.object.fetchSwapRemove("replayGainTrackPeak"); // float, int, or missing
            // it's an array of signed integers, or missing
            _ = track_json.object.fetchSwapRemove("fingerprint");
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
            _ = ldir_json.object.fetchSwapRemove("mtime").?.value.string;

            for (ldir_json.object.keys()) |k| {
                log.err("unknown key for LibraryDir '{s}': '{s}'", .{ root_key, k });
            }
        } else if (mem.startsWith(u8, root_key, "Player.")) {
            if (mem.eql(u8, root_key, "Player.autoDjOn")) {
                // bool
            } else if (mem.eql(u8, root_key, "Player.currentTrackInfo")) {
                // object. example:
                // "id": "jwRsBxnogVofMZbmmGgLqbtRHrf80NJ_",
                // "pos": 1.784
            } else if (mem.eql(u8, root_key, "Player.dynamicModeOn")) {
                // bool
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
            _ = pl_json.object.fetchSwapRemove("name").?.value.string;
            _ = pl_json.object.fetchSwapRemove("mtime").?.value.integer;

            for (pl_json.object.keys()) |k| {
                log.err("unknown key for playlist item '{s}': '{s}'", .{ root_key, k });
            }
        } else if (mem.startsWith(u8, root_key, "Users.")) {
            var user_json = try std.json.parseFromSliceLeaky(std.json.Value, arena, root_value.string, .{});
            _ = user_json.object.fetchSwapRemove("id").?.value.string;
            _ = user_json.object.fetchSwapRemove("name").?.value.string;
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
}
