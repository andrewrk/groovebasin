const std = @import("std");
const AutoArrayHashMapUnmanaged = std.AutoArrayHashMapUnmanaged;
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

const items = &db.TheDatabase.items;
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
var groove_files: AutoArrayHashMapUnmanaged(Id, *Groove.File) = .{};

const SeekRequest = struct {
    id: Id,
    pos: f64,
};

const Item = db.Item;

pub fn init() !void {
    seek_request = null;
    current_item = null;
}

pub fn deinit() void {
    groove_files.deinit(g.gpa);
}

/// After `items` has been initialized from disk, populate `groove_files` from `items`.
pub fn handleLoaded() !void {
    std.debug.assert(groove_files.count() == 0);
    var it = items.iterator();
    while (it.next()) |kv| {
        const item_id = kv.key_ptr.*;
        const library_key = kv.value_ptr.track_key;
        const groove_file = library.loadGrooveFile(library_key) catch |err| switch (err) {
            error.OutOfMemory => return error.OutOfMemory,
            error.TrackNotFound => {
                log.warn("enqueuing id {} failed: track not found", .{item_id});
                continue;
            },
            error.LoadFailure => {
                continue;
            },
        };
        errdefer groove_file.destroy();

        try groove_files.putNoClobber(g.gpa, item_id, groove_file);
    }
    updateLibGroovePlaylist();
}

pub fn seek(changes: *db.Changes, user_id: Id, id: Id, pos: f64) !void {
    // TODO: add the seek event ("poopsmith3 seeked to a different song")
    _ = user_id;

    seek_request = .{
        .id = id,
        .pos = pos,
    };
    updateLibGroovePlaylist();
    changes.broadcastChanges(.state);
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
    } else if (items.table.count() > 0) {
        sort();
        current_item = .{
            .id = items.table.keys()[0],
            .state = .{ .playing = std.time.milliTimestamp() },
        };
    }

    log.debug("groove playlist play", .{});
    updateLibGroovePlaylist();
    g.player.playlist.play();
    changes.broadcastChanges(.state);
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
    changes.broadcastChanges(.state);
}

pub fn enqueue(changes: *db.Changes, new_items: anytype) !void {
    try items.table.ensureUnusedCapacity(g.gpa, new_items.map.count());
    var it = new_items.map.iterator();
    while (it.next()) |kv| {
        const item_id = kv.key_ptr.*;
        const library_key = kv.value_ptr.key;
        const sort_key = kv.value_ptr.sortKey;
        if (items.contains(item_id)) {
            log.warn("ignoring queue item with id collision: {}", .{item_id});
            continue;
        }
        log.info("enqueuing: {}: {} @{s}", .{ item_id, library_key, sort_key });

        const groove_file = library.loadGrooveFile(library_key) catch |err| switch (err) {
            error.OutOfMemory => return error.OutOfMemory,
            error.TrackNotFound => {
                log.warn("enqueuing id {} failed: track not found", .{item_id});
                continue;
            },
            error.LoadFailure => {
                continue;
            },
        };
        errdefer groove_file.destroy();

        try groove_files.putNoClobber(g.gpa, item_id, groove_file);
        items.putNoClobber(changes, item_id, .{
            .sort_key = sort_key,
            .track_key = library_key,
            .is_random = false,
        }) catch unreachable; // assume capacity
    }
    updateLibGroovePlaylist();
}

pub fn move(changes: *db.Changes, args: anytype) !void {
    var it = args.map.iterator();
    while (it.next()) |kv| {
        const item_id = kv.key_ptr.*;
        const sort_key = kv.value_ptr.sortKey;
        if (!items.contains(item_id)) {
            log.warn("attempt to move non-existent item: {}", .{item_id});
            continue;
        }
        const item = try items.getForEditing(changes, item_id);
        log.info("moving: {}: @{} -> @{}", .{ item_id, item.sort_key, sort_key });
        item.sort_key = sort_key;
        // TODO: check for collisions?
    }
    updateLibGroovePlaylist();
}

pub fn remove(changes: *db.Changes, args: []Id) !void {
    for (args) |item_id| {
        if (!items.contains(item_id)) {
            log.warn("ignoring attempt to remove non-existent item: {}", .{item_id});
            continue;
        }
        items.remove(changes, item_id);
        groove_files.fetchSwapRemove(item_id).?.value.destroy();
    }
}

pub fn getSerializable(arena: std.mem.Allocator, out_version: *?Id) !IdMap(protocol.QueueItem) {
    out_version.* = Id.random(); // TODO: versioning
    var result = IdMap(protocol.QueueItem){};
    try result.map.ensureTotalCapacity(arena, items.table.count());

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
            const a_sort_key = items.table.values()[a_index].sort_key;
            const b_sort_key = items.table.values()[b_index].sort_key;
            return keese.order(a_sort_key, b_sort_key) == .lt;
        }
    };
    items.table.sort(SortContext{});
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
    const ids = items.table.keys();
    var groove_item = g.player.playlist.head;
    var seen_current_track = false;
    for (ids) |id| {
        if (!seen_current_track) {
            if (id.value == cur_item.id.value) {
                seen_current_track = true;
            } else if (groove_item) |gi| {
                groove_item = gi.next;
                g.player.playlist.remove(gi);
                continue;
            }
        }

        const groove_file = groove_files.get(id).?;
        if (groove_item) |gi| {
            if (gi.file == groove_file) {
                // Already synchronized.
                groove_item = gi.next;
            } else {
                // File does not match; insert new libgroove queue item before this one.
                _ = g.player.playlist.insert(groove_file, 1.0, 1.0, gi) catch
                    @panic("TODO handle this OOM");
            }
        } else {
            _ = g.player.playlist.insert(groove_file, 1.0, 1.0, null) catch
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
