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
const library = @import("library.zig");
const subscriptions = @import("subscriptions.zig");

const items = &g.the_database.items;
var current_item: ?struct {
    id: Id,
    state: union(enum) {
        /// track start date in milliseconds relative to now
        playing: i64,
        /// seconds into the song where the seek head is paused
        paused: f64,
    },
} = undefined;

// This tracks Groove data for each queue item.
var groove_data: AutoArrayHashMapUnmanaged(Id, ItemGrooveData) = .{};
const ItemGrooveData = struct {
    file: *Groove.File,
    playlist_item: *Groove.Playlist.Item,
};

const Item = db.Item;

pub fn init() !void {
    current_item = null;
}

pub fn deinit() void {
    for (groove_data.values()) |data| {
        g.player.playlist.remove(data.playlist_item);
        data.file.destroy();
    }
    groove_data.deinit(g.gpa);
}

/// After `items` has been initialized from disk, populate `groove_data` from `items`.
pub fn handleLoaded() !void {
    std.debug.assert(groove_data.count() == 0);

    if (current_item != null and current_item.?.state == .playing) {
        // Server shutdown while playing. We don't know when the server
        // shutdown, so we have no way to reconstruct where we were in this
        // song. Seek back to the start.
        current_item.?.state = .{ .paused = 0.0 };
    }

    sort();

    var following_playlist_item: ?*Groove.Playlist.Item = null;
    var i: usize = items.table.count();
    while (i > 0) : (i -= 1) {
        const item_id = items.table.keys()[i];
        const item = items.table.values()[i];
        const groove_file = library.loadGrooveFile(item.track_key) catch |err| switch (err) {
            error.OutOfMemory => return error.OutOfMemory,
            error.TrackNotFound, error.LoadFailure => {
                log.warn("cleaning unloadable queue item on startup: {}: {s}", .{ item_id, @errorName(err) });
                items.table.orderedRemoveAt(i);
                if (current_item != null and current_item.?.id == item_id) {
                    // Need to bump current track foward.
                    if (i == items.table.count()) {
                        // I mean backward.
                        if (items.table.count() == 0) {
                            // I mean.
                            current_item = null;
                        } else {
                            current_item = .{
                                .id = items.keys()[i - 1],
                                .state = .{ .paused = 0.0 },
                            };
                        }
                    } else {
                        current_item = .{
                            .id = items.keys()[i],
                            .state = .{ .paused = 0.0 },
                        };
                    }
                }
                continue;
            },
        };
        errdefer groove_file.destroy();

        const playlist_item = try g.player.playlist.insert(groove_file, 1.0, 1.0, following_playlist_item);
        errdefer g.player.playlist.remove(playlist_item);

        try groove_data.putNoClobber(g.gpa, item_id, .{
            .file = groove_file,
            .playlist_item = playlist_item,
        });

        following_playlist_item = playlist_item; // We're iterating backwards.
    }

    if (current_item != null) {
        // Seek paused
        const data = groove_data.get(current_item.?.id).?;
        g.player.playlist.seek(data.playlist_item, current_item.?.state.paused);
    }
}

pub fn seek(user_id: Id, id: Id, pos: f64) !void {
    // TODO: add the seek event ("poopsmith3 seeked to a different song")
    _ = user_id;

    if (!items.contains(id)) {
        log.warn("ignoring seek for bogus queue item: {}", .{id});
        return;
    }

    const new_start_time = std.time.milliTimestamp() - @as(f64, @intFromFloat(pos * 1000.0));

    const is_playing = current_item != null and current_item.?.state == .playing;
    if (is_playing) {
        current_item = .{
            .id = id,
            .state = .{ .playing = new_start_time },
        };
    } else {
        current_item = .{
            .id = id,
            .state = .{ .paused = pos },
        };
    }

    const data = groove_data.get(id).?;
    g.player.playlist.seek(data.playlist_item, pos);
}

pub fn play(user_id: Id) !void {
    // TODO: add the play event ("poopsmith3 pressed play")
    _ = user_id;
    if (current_item == null) {
        log.warn("ignoring play without any seek", .{});
        return;
    }

    switch (current_item.?.state) {
        .playing => {
            log.warn("ignoring play while already playing", .{});
            return;
        },
        .paused => |pos| {
            const new_start_time = std.time.milliTimestamp() - @as(f64, @intFromFloat(pos * 1000.0));
            current_item.?.state = .{ .playing = new_start_time };
            g.player.playlist.play();
        },
    }
}

pub fn pause(user_id: Id) !void {
    // TODO: add the pause event ("poopsmith3 pressed pause")
    _ = user_id;

    if (current_item == null) {
        log.warn("ignoring pause without any seek", .{});
        return;
    }

    switch (current_item.?.state) {
        .paused => {
            log.warn("ignoring pause while already paused", .{});
            return;
        },
        .playing => |track_start_date| {
            g.player.playlist.pause();
            const pos = @as(f64, @floatFromInt(std.time.milliTimestamp() - track_start_date)) / 1000.0;
            current_item = .{
                .id = cur.id,
                .state = .{ .paused = pos },
            };
        },
    }
}

pub fn enqueue(new_items: anytype) !void {
    try items.table.ensureUnusedCapacity(g.gpa, new_items.map.count());
    try groove_data.ensureUnusedCapacity(g.gpa, new_items.map.count());

    var it = new_items.map.iterator();
    while (it.next()) |kv| {
        const item_id = kv.key_ptr.*;
        const library_key = kv.value_ptr.key;
        const sort_key = kv.value_ptr.sortKey;
        if (items.contains(item_id)) {
            log.warn("ignoring queue item with id collision: {}", .{item_id});
            continue;
        }
        log.info("enqueuing: {}: {} @{}", .{ item_id, library_key, sort_key });

        items.putNoClobber(item_id, .{
            .sort_key = sort_key,
            .track_key = library_key,
            .is_random = false,
        }) catch unreachable; // assume capacity
    }
    errdefer {
        for (new_items.map.keys()) |key| {
            items.swapRemove(key);
            _ = groove_data.swapRemove(key);
        }
    }

    sort();

    // Tell groove about it.
    var i: usize = new_items.map.count() - 1;
    while (i > 0) : (i -= 1) {
        const key = new_items.map.keys()[i - 1];
        const main_index = items.table.getIndex(key);
        const library_key = items.table.values()[main_index].track_key;

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

        const following_item = if (main_index < items.table.count() - 1)
            groove_data.get(items.table.keys()[main_index + 1]).?.playlist_item
        else
            null;
        const playlist_item = try g.player.playlist.insert(groove_file, 1.0, 1.0, following_item);

        groove_data.putAssumeCapacityNoClobber(item_id, .{
            .file = groove_file,
            .playlist_item = playlist_item,
        });
    }
}

pub fn move(args: anytype) !void {
    try items.modified_entries.ensureUnusedCapacity(g.gpa, args.map.count());
    {
        var it = args.map.iterator();
        while (it.next()) |kv| {
            const item_id = kv.key_ptr.*;
            const sort_key = kv.value_ptr.*;
            if (!items.contains(item_id)) {
                log.warn("attempt to move non-existent item: {}", .{item_id});
                continue;
            }
            const item = items.getForEditing(item_id) catch unreachable; // assume capacity
            log.info("moving: {}: @{} -> @{}", .{ item_id, item.sort_key, sort_key });
            item.sort_key = sort_key;
        }
    }

    sort();
    // TODO: check for collisions
    var it = items.iterator();
    var previous_sort_key = -std.math.inf(f64);
    while (it.next()) |kv| {
        const sort_key = kv.value_ptr.sort_key;
        if (previous_sort_key >= sort_key) {
            const item = try it.promoteForEditing(kv);
        }
    }

    // Tell groove about it.
    const current_item_index = if (current_item != null)
        items.table.getIndex(current_item.?.id).?
    else
        null;
    var following_item: ?*Groove.Playlist.Item = null;
    var i: usize = items.table.count() - 1;
    while (i > 0) : (i -= 1) {
        const index = i - 1;
        const key = items.table.keys()[index];
        const data = groove_data.getEntry(key).?.value_ptr;

        if (data.playlist_item.next == following_item) {
            // Still correct.
        } else {
            // This one is out of order.
            if (index == current_item_index) {
                // We must not remove the playlist item that's currently playing.
                // Instead, swap the current item forward repeatedly until it's in its intended destination.
                while (data.playlist_item.next != following_item) {
                    const next_groove_data = lookupPlaylistItem(data.playlist_item.next.?);
                    g.player.playlist.remove(next_groove_data.playlist_item);
                    next_groove_data.playlist_item = try g.player.playlist.insert(
                        next_groove_data.file,
                        1.0,
                        1.0,
                        data.playlist_item,
                    );
                }
            } else {
                // Remove and re-insert this item.
                g.player.playlist.remove(data.playlist_item);
                data.playlist_item = try g.player.playlist.insert(data.file, 1.0, 1.0, following_item);
            }
        }
        following_item = data.playlist_item;
    }
}

fn lookupPlaylistItem(item: *Groove.Player.Item) *ItemGrooveData {
    // TODO: More efficient way to look this up.
    //       Alternatively, only maintain 2 groove items instead of the whole list.
    var it = groove_data.iterator();
    while (it.next()) |kv| {
        if (kv.value_ptr.playlist_item == item) return kv.value_ptr;
    }
    unreachable;
}

pub fn remove(args: []Id) !void {
    try items.removed_keys.ensureUnusedCapacity(g.gpa, args.len);

    sort();
    for (args) |item_id| {
        if (!items.contains(item_id)) {
            log.warn("ignoring attempt to remove non-existent item: {}", .{item_id});
            continue;
        }
        const i = items.table.getKey(item_id).?;
        items.orderedRemove(item_id) catch unreachable; // assume capacity
        groove_data.fetchSwapRemove(item_id).?.value.destroy();

        if (current_item != null and current_item.?.id == item_id) {
            // TODO: this logic is unsound.
            // Need to bump current track foward.
            if (i == items.table.count() - 1) {
                // I mean backward.
                if (items.table.count() == 0) {
                    // I mean.
                    current_item = null;
                } else {
                    current_item = .{
                        .id = items.keys()[i],
                        .state = .{ .paused = 0.0 },
                    };
                }
            } else {
                current_item = .{
                    .id = items.keys()[i + 1],
                    .state = .{ .paused = 0.0 },
                };
            }
            // TODO also modify the groove playlist.
        }
    }
}

pub fn serializableItem(item: Item) protocol.QueueItem {
    return .{
        .sortKey = item.sort_key,
        .key = item.track_key,
        .isRandom = item.is_random,
    };
}

pub fn serializableCurrentTrack(_: void) protocol.CurrentTrack {
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
            return a_sort_key < b_sort_key;
        }
    };
    items.table.sort(SortContext{});
}
