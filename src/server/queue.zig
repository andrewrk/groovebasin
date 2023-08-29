const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;
const log = std.log;

const g = @import("global.zig");
const db = @import("db.zig");
const Groove = @import("groove.zig").Groove;

const protocol = @import("groovebasin_protocol.zig");
const Id = protocol.Id;
const IdMap = protocol.IdMap;
const keese = @import("keese.zig");
const library = @import("library.zig");
const subscriptions = @import("subscriptions.zig");

var current_version: Id = undefined;
var items: AutoArrayHashMap(Id, Item) = undefined;
var seek_request: ?SeekRequest = undefined;
var current_item: ?struct {
    id: Id,
    state: union(enum) {
        /// track start date in milliseconds relative to now
        playing: i64,
        /// milliseconds into the song where the seek head is paused
        paused: i64,
    },
} = undefined;

const SeekRequest = struct {
    id: Id,
    pos: f64,
};

const Item = struct {
    sort_key: keese.Value,
    track_key: Id,
    is_random: bool,
    groove_file: *Groove.File,
};

pub fn init() !void {
    current_version = Id.random();
    items = AutoArrayHashMap(Id, Item).init(g.gpa);
    seek_request = null;
    current_item = null;
}

pub fn deinit() void {
    items.deinit();
}

pub fn seek(changes: *db.Changes, user_id: Id, id: Id, pos: f64) !void {
    // TODO: add the seek event ("poopsmith3 seeked to a different song")
    _ = user_id;

    seek_request = .{
        .id = id,
        .pos = pos,
    };
    updateLibGroovePlaylist();
    changes.broadcastChanges(.currentTrack);
}

pub fn play(changes: *db.Changes, user_id: Id) !void {
    // TODO: add the play event ("poopsmith3 pressed play")
    _ = user_id;

    if (current_item) |cur| switch (cur.state) {
        .playing => return, // already playing
        .paused => |paused_time| {
            current_item = .{
                .id = cur.id,
                .state = .{ .playing = std.time.milliTimestamp() - paused_time },
            };
        },
    } else if (items.count() > 0) {
        sort();
        current_item = .{
            .id = items.keys()[0],
            .state = .{ .playing = std.time.milliTimestamp() },
        };
    }

    log.debug("groove playlist play", .{});
    updateLibGroovePlaylist();
    g.player.playlist.play();
    changes.broadcastChanges(.currentTrack);
}

pub fn pause(changes: *db.Changes, user_id: Id) !void {
    // TODO: add the pause event ("poopsmith3 pressed pause")
    _ = user_id;

    if (current_item) |cur| switch (cur.state) {
        .paused => return, // already paused
        .playing => |track_start_date| {
            current_item = .{
                .id = cur.id,
                .state = .{ .paused = std.time.milliTimestamp() - track_start_date },
            };
        },
    };

    updateLibGroovePlaylist();
    g.player.playlist.pause();
    changes.broadcastChanges(.currentTrack);
}

pub fn enqueue(arena: Allocator, new_items: anytype) !void {
    try items.ensureUnusedCapacity(new_items.map.count());
    var it = new_items.map.iterator();
    while (it.next()) |kv| {
        const item_id = kv.key_ptr.*;
        const library_key = kv.value_ptr.key;
        const sort_key = kv.value_ptr.sortKey;
        log.info("enqueuing: {}: {} @{s}", .{ item_id, library_key, sort_key });
        const gop = items.getOrPutAssumeCapacity(item_id);
        if (gop.found_existing) {
            log.warn("ignoring queue item with id collision: {}", .{item_id});
            continue;
        }
        errdefer _ = items.pop();

        const groove_file = library.loadGrooveFile(library_key) catch |err| switch (err) {
            error.OutOfMemory => return error.OutOfMemory,
            error.TrackNotFound => {
                log.warn("enqueuing id {} failed: track not found", .{item_id});
                _ = items.pop();
                continue;
            },
            error.LoadFailure => {
                _ = items.pop();
                continue;
            },
        };

        gop.value_ptr.* = .{
            .sort_key = sort_key,
            .track_key = library_key,
            .is_random = false,
            .groove_file = groove_file,
        };
    }
    current_version = Id.random();
    updateLibGroovePlaylist();
    try subscriptions.broadcastChanges(arena, .queue);
}

pub fn move(arena: Allocator, args: anytype) !void {
    var it = args.map.iterator();
    while (it.next()) |kv| {
        const item_id = kv.key_ptr.*;
        const sort_key = kv.value_ptr.sortKey;
        const item = (items.getEntry(item_id) orelse {
            log.warn("attempt to move non-existent item: {}", .{item_id});
            continue;
        }).value_ptr;
        log.info("moving: {}: @{} -> @{}", .{ item_id, item.sort_key, sort_key });
        item.sort_key = sort_key;
        // TODO: check for collisions?
    }
    current_version = Id.random();
    updateLibGroovePlaylist();
    try subscriptions.broadcastChanges(arena, .queue);
}

pub fn remove(arena: Allocator, args: []Id) !void {
    for (args) |item_id| {
        if (items.fetchSwapRemove(item_id)) |kv| {
            kv.value.groove_file.destroy();
        } else {
            log.warn("attempt to remove non-existent item: {}", .{item_id});
        }
    }
    current_version = Id.random();
    try subscriptions.broadcastChanges(arena, .queue);
}

pub fn getSerializable(arena: std.mem.Allocator, out_version: *?Id) !IdMap(protocol.QueueItem) {
    out_version.* = current_version;
    var result = IdMap(protocol.QueueItem){};
    try result.map.ensureTotalCapacity(arena, items.count());

    var it = items.iterator();
    while (it.next()) |kv| {
        const id = kv.key_ptr.*;
        const queue_item = kv.value_ptr.*;
        result.map.putAssumeCapacityNoClobber(id, itemToSerializedForm(queue_item));
    }
    return result;
}

fn itemToSerializedForm(item: Item) protocol.QueueItem {
    return .{
        .sortKey = item.sort_key,
        .key = item.track_key,
        .isRandom = item.is_random,
    };
}

pub fn getSerializedCurrentTrack() protocol.CurrentTrack {
    const item = current_item orelse return .{
        .currentItemId = null,
        .isPlaying = false,
        .trackStartDate = null,
        .pausedTime = null,
    };
    return switch (item.state) {
        .playing => |track_start_date| .{
            .currentItemId = item.id,
            .isPlaying = true,
            .trackStartDate = .{ .value = track_start_date },
            .pausedTime = null,
        },
        .paused => |paused_time| .{
            .currentItemId = item.id,
            .isPlaying = false,
            .trackStartDate = null,
            .pausedTime = @as(f64, @floatFromInt(paused_time)) / 1000.0,
        },
    };
}

fn sort() void {
    const SortContext = struct {
        pub fn lessThan(ctx: @This(), a_index: usize, b_index: usize) bool {
            _ = ctx; // we're using global variables
            const a_sort_key = items.values()[a_index].sort_key;
            const b_sort_key = items.values()[b_index].sort_key;
            return keese.order(a_sort_key, b_sort_key) == .lt;
        }
    };
    items.sort(SortContext{});
}

fn updateLibGroovePlaylist() void {
    const cur_item = current_item orelse {
        g.player.playlist.clear();
        return;
    };

    sort();

    // Iterate until the current track, deleting libgroove queue items until that point.
    // Once we hit the current track, start synchronizing the groovebasin queue with the
    // libgroove playlist.
    const ids = items.keys();
    const queue_items = items.values();
    var groove_item = g.player.playlist.head;
    var seen_current_track = false;
    for (ids, queue_items) |id, item| {
        if (!seen_current_track) {
            if (id.value == cur_item.id.value) {
                seen_current_track = true;
            } else if (groove_item) |gi| {
                groove_item = gi.next;
                g.player.playlist.remove(gi);
                continue;
            }
        }

        if (groove_item) |gi| {
            if (gi.file == item.groove_file) {
                // Already synchronized.
                groove_item = gi.next;
            } else {
                // File does not match; insert new libgroove queue item before this one.
                _ = g.player.playlist.insert(item.groove_file, 1.0, 1.0, gi) catch
                    @panic("TODO handle this OOM");
            }
        } else {
            _ = g.player.playlist.insert(item.groove_file, 1.0, 1.0, null) catch
                @panic("TODO handle this OOM");
        }
    }

    // Remove the remaining libgroove playlist items, which do not appear on
    // the groovebasin play queue.
    while (groove_item) |gi| {
        groove_item = gi.next;
        g.player.playlist.remove(gi);
    }
}
