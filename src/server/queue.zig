const std = @import("std");
const assert = std.debug.assert;
const AutoArrayHashMapUnmanaged = std.AutoArrayHashMapUnmanaged;
const Allocator = std.mem.Allocator;
const log = std.log;

const Db = @import("db.zig").TheDatabase;
const g = @import("global.zig");
const Groove = @import("groove.zig").Groove;
const Player = @import("Player.zig");

const protocol = @import("groovebasin_protocol.zig");
const Id = protocol.Id;
const IdMap = protocol.IdMap;
const library = @import("library.zig");
const subscriptions = @import("subscriptions.zig");

const Queue = @This();

map: AutoArrayHashMapUnmanaged(*Groove.Playlist.Item, Id) = .{},

pub fn handleLoaded(q: *Queue) !void {
    const db = &g.the_database;
    const player = &g.player;

    if (db.state.current_item != null and db.state.current_item.?.state == .playing) {
        // Server shutdown while playing. We don't know when the server
        // shutdown, so we have no way to reconstruct where we were in this
        // song. Seek back to the start.
        db.state.current_item.?.state = .{ .paused = 0.0 };
    }

    sort();
    try q.lowerToLibGroove(player, &g.the_database);

    // Seek and pause.
    if (player.playlist.head) |head| {
        player.playlist.seek(head, db.state.current_item.?.state.paused);
    }
}

pub fn seek(q: *Queue, user_id: Id, id: Id, pos: f64) !void {
    // TODO: add the seek event ("poopsmith3 seeked to a different song")
    _ = user_id;
    const db = &g.the_database;
    const player = &g.player;

    if (!db.items.contains(id)) {
        log.warn("ignoring seek for bogus queue item: {}", .{id});
        return;
    }

    const is_playing = db.state.current_item != null and db.state.current_item.?.state == .playing;
    if (is_playing) {
        const new_start_time = std.time.milliTimestamp() - @as(i64, @intFromFloat(pos * 1000.0));
        db.state.current_item = .{
            .id = id,
            .state = .{ .playing = new_start_time },
        };
    } else {
        db.state.current_item = .{
            .id = id,
            .state = .{ .paused = pos },
        };
    }

    try q.lowerToLibGroove(player, &g.the_database);

    if (player.playlist.head) |head| {
        player.playlist.seek(head, pos);
    }
}

pub fn play(q: *Queue, user_id: Id) !void {
    _ = q;
    // TODO: add the play event ("poopsmith3 pressed play")
    _ = user_id;
    const db = &g.the_database;

    if (db.state.current_item == null) {
        log.warn("ignoring play without any seek", .{});
        return;
    }

    switch (db.state.current_item.?.state) {
        .playing => {
            log.warn("ignoring play while already playing", .{});
            return;
        },
        .paused => |pos| {
            const new_start_time = std.time.milliTimestamp() - @as(i64, @intFromFloat(pos * 1000.0));
            db.state.current_item.?.state = .{ .playing = new_start_time };
            g.player.playlist.play();
        },
    }
}

pub fn pause(q: *Queue, user_id: Id) !void {
    _ = q;
    // TODO: add the pause event ("poopsmith3 pressed pause")
    _ = user_id;
    const db = &g.the_database;

    if (db.state.current_item == null) {
        log.warn("ignoring pause without any seek", .{});
        return;
    }

    switch (db.state.current_item.?.state) {
        .paused => {
            log.warn("ignoring pause while already paused", .{});
            return;
        },
        .playing => |track_start_date| {
            g.player.playlist.pause();
            const pos = @as(f64, @floatFromInt(std.time.milliTimestamp() - track_start_date)) / 1000.0;
            db.state.current_item.?.state = .{ .paused = pos };
        },
    }
}

pub fn stop(q: *Queue, user_id: Id) !void {
    _ = q;
    // TODO: add the pause event ("poopsmith3 pressed stop")
    _ = user_id;
    const db = &g.the_database;

    if (db.state.current_item == null) {
        log.warn("ignoring stop without any current item", .{});
        return;
    }

    switch (db.state.current_item.?.state) {
        .paused => {
            // Pressing stop while already paused effectively just seeks to 0.0 seconds.
        },
        .playing => {
            g.player.playlist.pause();
        },
    }
    db.state.current_item.?.state = .{ .paused = 0.0 };
}

pub fn enqueue(q: *Queue, new_items: anytype) !void {
    const db = &g.the_database;
    try db.items.table.ensureUnusedCapacity(g.gpa, new_items.map.count());
    try db.items.added_keys.ensureUnusedCapacity(g.gpa, new_items.map.count());

    var it = new_items.map.iterator();
    while (it.next()) |kv| {
        const item_id = kv.key_ptr.*;
        const library_key = kv.value_ptr.key;
        const sort_key = kv.value_ptr.sortKey;
        if (db.items.contains(item_id)) {
            log.warn("ignoring queue item with id collision: {}", .{item_id});
            continue;
        }
        log.info("enqueuing: {}: {} @{}", .{ item_id, library_key, sort_key });

        db.items.putNoClobber(item_id, .{
            .sort_key = sort_key,
            .track_key = library_key,
            .is_random = false,
        }) catch unreachable; // assume capacity
    }

    sort();
    try q.lowerToLibGroove(&g.player, &g.the_database);
}

pub fn move(q: *Queue, args: anytype) !void {
    const db = &g.the_database;
    try db.items.modified_entries.ensureUnusedCapacity(g.gpa, args.map.count());
    {
        var it = args.map.iterator();
        while (it.next()) |kv| {
            const item_id = kv.key_ptr.*;
            const sort_key = kv.value_ptr.*;
            if (!db.items.contains(item_id)) {
                log.warn("attempt to move non-existent item: {}", .{item_id});
                continue;
            }
            const item = db.items.getForEditing(item_id) catch unreachable; // assume capacity
            log.info("moving: {}: @{} -> @{}", .{ item_id, item.sort_key, sort_key });
            item.sort_key = sort_key;
        }
    }

    sort();
    // Check for collisions.
    var it = db.items.iterator();
    var previous_sort_key = -std.math.inf(f64);
    while (it.next()) |kv| {
        const sort_key = kv.value_ptr.sort_key;
        if (previous_sort_key >= sort_key) {
            const item = try it.promoteForEditing(kv);
            // This does not reorder the items.
            item.sort_key = addSomeSmallAmount(previous_sort_key);
            previous_sort_key = item.sort_key;
        } else {
            previous_sort_key = sort_key;
        }
    }

    try q.lowerToLibGroove(&g.player, &g.the_database);
}

/// Returns a value strictly f(x) > x by a relatively small amount.
fn addSomeSmallAmount(x: f64) f64 {
    const result = x + @max(@abs(x * 0.00001), 0.00001);
    return result;
}

pub fn remove(q: *Queue, args: []Id) !void {
    const db = &g.the_database;
    try db.items.removed_keys.ensureUnusedCapacity(g.gpa, args.len);

    var recover_current_item_after_sort_key: ?f64 = null;
    for (args) |item_id| {
        if (!db.items.contains(item_id)) {
            log.warn("ignoring attempt to remove non-existent item: {}", .{item_id});
            continue;
        }

        // Current item dodges the crater.
        if (db.state.current_item != null and db.state.current_item.?.id.value == item_id.value) {
            // Find the next item later.
            recover_current_item_after_sort_key = db.items.get(item_id).sort_key;
        }

        db.items.remove(item_id) catch unreachable; // assume capacity
    }

    sort();
    if (recover_current_item_after_sort_key) |sort_key| {
        // TODO: binary search?
        var it = db.items.iterator();
        while (it.next()) |kv| {
            if (kv.value_ptr.sort_key > sort_key) {
                setCurrentItemId(kv.key_ptr.*);
                break;
            }
        } else {
            // Last item deleted.
            db.state.current_item = null;
        }
    }

    try q.lowerToLibGroove(&g.player, &g.the_database);
}

/// Ensures the intended current item and following item are loaded into Groove.
/// Assumes the play queue is already sorted.
fn lowerToLibGroove(q: *Queue, player: *Player, db: *Db) error{OutOfMemory}!void {
    const gpa = g.gpa;

    const current_libgroove_item = c: {
        var current_libgroove_item: ?*Groove.Playlist.Item = undefined;
        player.playlist.position(&current_libgroove_item, null);
        break :c current_libgroove_item;
    };

    // Delete all libgroove playlist items before the currently playing track.
    while (player.playlist.head != current_libgroove_item) {
        const head = player.playlist.head orelse break;
        const file = head.file;
        player.playlist.remove(head);
        assert(q.map.swapRemove(head));
        file.destroy();
    }

    const current_db_item = db.state.current_item orelse return;

    // Keep the current track and subsequent ones that are correct, deleting
    // the ones that are not.
    var db_track_index: usize = db.items.table.getIndex(current_db_item.id).?;
    var libgroove_item_it = current_libgroove_item;
    const queue_ids = db.items.table.keys();
    outer: while (db_track_index < queue_ids.len) {
        const db_id = queue_ids[db_track_index];
        // Delete from libgroove if non-matching.
        while (true) {
            const libgroove_item = libgroove_item_it orelse break :outer;
            if (db_id.value == q.map.get(libgroove_item).?.value) {
                db_track_index += 1;
                libgroove_item_it = libgroove_item.next;
                continue :outer;
            }
            const next = libgroove_item.next;
            const file = libgroove_item.file;
            player.playlist.remove(libgroove_item);
            assert(q.map.swapRemove(libgroove_item));
            file.destroy();
            libgroove_item_it = next;
        }
    }

    // Add missing items to the play queue.
    try q.map.ensureUnusedCapacity(gpa, queue_ids.len - db_track_index);
    for (queue_ids[db_track_index..], db.items.table.values()[db_track_index..]) |queue_id, queue_item| {
        const groove_file = library.loadGrooveFile(queue_item.track_key) catch |err| switch (err) {
            error.OutOfMemory => return error.OutOfMemory,
            error.LoadFailure => @panic("TODO"),
            error.TrackNotFound => unreachable,
        };
        errdefer groove_file.destroy();

        // Append playlist item.
        const TODO_replaygain = 0.3;
        const libgroove_item = try player.playlist.insert(groove_file, TODO_replaygain, TODO_replaygain, null);
        q.map.putAssumeCapacityNoClobber(libgroove_item, queue_id);
    }
}

fn setCurrentItemId(item_id: Id) void {
    const db = &g.the_database;

    db.state.current_item = .{
        .id = item_id,
        .state = if (db.state.current_item.?.state == .playing)
            .{ .playing = std.time.milliTimestamp() }
        else
            .{ .paused = 0.0 },
    };
}

pub fn serializableItem(item: Db.Item) protocol.QueueItem {
    return .{
        .sortKey = item.sort_key,
        .key = item.track_key,
        .isRandom = item.is_random,
    };
}

pub fn serializableCurrentTrack(current_track: anytype) protocol.CurrentTrack {
    const item = current_track orelse return .{
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
            .pausedTime = paused_time,
        },
    };
}

fn sort() void {
    const db = &g.the_database;
    const SortContext = struct {
        values: []Db.Item,
        pub fn lessThan(ctx: @This(), a_index: usize, b_index: usize) bool {
            const a_sort_key = ctx.values[a_index].sort_key;
            const b_sort_key = ctx.values[b_index].sort_key;
            return a_sort_key < b_sort_key;
        }
    };
    db.items.table.sort(SortContext{ .values = db.items.table.values() });
}
