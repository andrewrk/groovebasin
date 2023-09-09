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
const current_item = &g.the_database.state.current_item;

// This tracks Groove data for each queue item.
var groove_datas: [2]?struct {
    item_id: Id,
    file: *Groove.File,
    playlist_item: *Groove.Playlist.Item,
} = .{ null, null };

const Item = db.Item;

pub fn init() !void {}

pub fn deinit() void {
    clearGrooveDatas();
}

pub fn handleLoaded() !void {
    if (current_item.* != null and current_item.*.?.state == .playing) {
        // Server shutdown while playing. We don't know when the server
        // shutdown, so we have no way to reconstruct where we were in this
        // song. Seek back to the start.
        current_item.*.?.state = .{ .paused = 0.0 };
    }

    sort();
    try tellGrooveAboutTheCurrentItem();

    if (current_item.* != null) {
        // Seek paused
        g.player.playlist.seek(groove_datas[0].?.playlist_item, current_item.*.?.state.paused);
    }
}

pub fn seek(user_id: Id, id: Id, pos: f64) !void {
    // TODO: add the seek event ("poopsmith3 seeked to a different song")
    _ = user_id;

    if (!items.contains(id)) {
        log.warn("ignoring seek for bogus queue item: {}", .{id});
        return;
    }

    const is_playing = current_item.* != null and current_item.*.?.state == .playing;
    if (is_playing) {
        const new_start_time = std.time.milliTimestamp() - @as(i64, @intFromFloat(pos * 1000.0));
        current_item.* = .{
            .id = id,
            .state = .{ .playing = new_start_time },
        };
    } else {
        current_item.* = .{
            .id = id,
            .state = .{ .paused = pos },
        };
    }

    try tellGrooveAboutTheCurrentItem();

    g.player.playlist.seek(groove_datas[0].?.playlist_item, pos);
}

pub fn play(user_id: Id) !void {
    // TODO: add the play event ("poopsmith3 pressed play")
    _ = user_id;
    if (current_item.* == null) {
        log.warn("ignoring play without any seek", .{});
        return;
    }

    switch (current_item.*.?.state) {
        .playing => {
            log.warn("ignoring play while already playing", .{});
            return;
        },
        .paused => |pos| {
            const new_start_time = std.time.milliTimestamp() - @as(i64, @intFromFloat(pos * 1000.0));
            current_item.*.?.state = .{ .playing = new_start_time };
            g.player.playlist.play();
        },
    }
}

pub fn pause(user_id: Id) !void {
    // TODO: add the pause event ("poopsmith3 pressed pause")
    _ = user_id;

    if (current_item.* == null) {
        log.warn("ignoring pause without any seek", .{});
        return;
    }

    switch (current_item.*.?.state) {
        .paused => {
            log.warn("ignoring pause while already paused", .{});
            return;
        },
        .playing => |track_start_date| {
            g.player.playlist.pause();
            const pos = @as(f64, @floatFromInt(std.time.milliTimestamp() - track_start_date)) / 1000.0;
            current_item.*.?.state = .{ .paused = pos };
        },
    }
}

pub fn enqueue(new_items: anytype) !void {
    try items.table.ensureUnusedCapacity(g.gpa, new_items.map.count());
    try items.added_keys.ensureUnusedCapacity(g.gpa, new_items.map.count());

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

    sort();
    try tellGrooveAboutTheCurrentItem();
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
    // Check for collisions.
    var it = items.iterator();
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

    try tellGrooveAboutTheCurrentItem();
}

/// Returns a value strictly f(x) > x by a relatively small amount.
fn addSomeSmallAmount(x: f64) f64 {
    const result = x + @max(@fabs(x * 0.00001), 0.00001);
    return result;
}

pub fn remove(args: []Id) !void {
    try items.removed_keys.ensureUnusedCapacity(g.gpa, args.len);

    var recover_current_item_after_sort_key: ?f64 = null;
    for (args) |item_id| {
        if (!items.contains(item_id)) {
            log.warn("ignoring attempt to remove non-existent item: {}", .{item_id});
            continue;
        }

        // Current item dodges the crater.
        if (current_item.* != null and current_item.*.?.id.value == item_id.value) {
            // Find the next item later.
            recover_current_item_after_sort_key = items.get(item_id).sort_key;
        }

        items.remove(item_id) catch unreachable; // assume capacity
    }

    sort();
    if (recover_current_item_after_sort_key) |sort_key| {
        // TODO: binary search?
        var it = items.iterator();
        while (it.next()) |kv| {
            if (kv.value_ptr.sort_key > sort_key) {
                setCurrentItemId(kv.key_ptr.*);
                break;
            }
        } else {
            // Last item deleted.
            current_item.* = null;
        }
    }

    try tellGrooveAboutTheCurrentItem();
}

// Ensures the intended current item and following item are loaded into Groove.
// If the current item needs to be loaded into Groove, then tells Groove to
// seek to the beginning of it (activating it). If we're supposed to seek into
// the middle of the song instead, do that seek after this function returns.
fn tellGrooveAboutTheCurrentItem() error{OutOfMemory}!void {
    errdefer {
        // Some unhandled error means stop everything.
        current_item.* = null;
        clearGrooveDatas();
    }

    // This only loops on library load error.
    found_broken_item_loop: while (current_item.* != null) {
        const item_id = current_item.*.?.id;

        // Drain wrong items.
        while (groove_datas[0] != null) {
            if (groove_datas[0].?.item_id.value == item_id.value) break; // correct
            // Nope. Skip this item.
            g.player.playlist.remove(groove_datas[0].?.playlist_item);
            groove_datas[0].?.file.destroy();
            // Shift, and then check the second one.
            groove_datas[0] = groove_datas[1];
            groove_datas[1] = null;
        }

        const index = items.table.getIndex(item_id).?;
        if (groove_datas[0] == null) {
            // Insert the current song.
            const groove_file = library.loadGrooveFile(items.get(item_id).track_key) catch |err| switch (err) {
                error.OutOfMemory => return error.OutOfMemory,
                error.LoadFailure => {
                    // Skip broken tracks.
                    if (index + 1 < items.table.count()) {
                        // Skip to the next one.
                        setCurrentItemId(items.table.keys()[index + 1]);
                    } else {
                        // Last item in the queue is broken. Stop playing.
                        current_item.* = null;
                    }
                    // Start over.
                    continue :found_broken_item_loop;
                },
                error.TrackNotFound => unreachable,
            };
            errdefer groove_file.destroy();

            const playlist_item = try g.player.playlist.insert(groove_file, 1.0, 1.0, null);
            groove_datas[0] = .{
                .item_id = item_id,
                .file = groove_file,
                .playlist_item = playlist_item,
            };

            // Groove just had no items in its playlist, so seek to the start
            // of its playlist. If we want to seek into the middle of this song
            // instead, that should be done after this function returns.
            g.player.playlist.seek(playlist_item, 0.0);
        }

        // And now for round 2!!!
        var second_index = index + 1;
        second_found_broken_item_loop: while (second_index < items.table.count()) {
            const second_item_id = items.table.keys()[second_index];
            // Drain any wrong item.
            if (groove_datas[1] != null) {
                if (groove_datas[1].?.item_id.value == second_item_id.value) break; // correct
                // Nope. Remove this one.
                g.player.playlist.remove(groove_datas[1].?.playlist_item);
                groove_datas[1].?.file.destroy();
                groove_datas[1] = null;
            }

            if (groove_datas[1] == null) {
                const groove_file = library.loadGrooveFile(items.get(second_item_id).track_key) catch |err| switch (err) {
                    error.OutOfMemory => return error.OutOfMemory,
                    error.LoadFailure => {
                        // Skip broken tracks.
                        second_index += 1;
                        // Start over.
                        continue :second_found_broken_item_loop;
                    },
                    error.TrackNotFound => unreachable,
                };
                errdefer groove_file.destroy();

                const playlist_item = try g.player.playlist.insert(groove_file, 1.0, 1.0, null);
                groove_datas[1] = .{
                    .item_id = second_item_id,
                    .file = groove_file,
                    .playlist_item = playlist_item,
                };
            }
            break;
        }

        break;
    } else {
        // Playing nothing.
        clearGrooveDatas();
    }
}

fn clearGrooveDatas() void {
    // Sometimes this function is called during error handlers, so don't rely
    // on the accuracy of our cached data.
    groove_datas = .{ null, null };
    // Cleanup everything from groove's own perspective.
    while (true) {
        var playlist_item: ?*Groove.Playlist.Item = undefined;
        g.player.playlist.position(&playlist_item, null);
        if (playlist_item == null) break;
        const groove_file = playlist_item.?.file;
        g.player.playlist.remove(playlist_item.?);
        groove_file.destroy();
    }
}

fn setCurrentItemId(item_id: Id) void {
    current_item.* = .{
        .id = item_id,
        .state = if (current_item.*.?.state == .playing)
            .{ .playing = std.time.milliTimestamp() }
        else
            .{ .paused = 0.0 },
    };
}

pub fn serializableItem(item: Item) protocol.QueueItem {
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
