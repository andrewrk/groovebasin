const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;
const log = std.log;

const g = @import("global.zig");

const groovebasin_protocol = @import("groovebasin_protocol.zig");
const Id = @import("groovebasin_protocol.zig").Id;
const IdMap = @import("groovebasin_protocol.zig").IdMap;
const keese = @import("keese.zig");
const subscriptions = @import("subscriptions.zig");

pub var current_queue_version: Id = undefined;
var items: AutoArrayHashMap(Id, InternalQueueItem) = undefined;

pub fn init() !void {
    current_queue_version = Id.random();
    items = AutoArrayHashMap(Id, InternalQueueItem).init(g.gpa);
}

pub fn deinit() void {
    items.deinit();
}

pub const InternalQueueItem = struct {
    sort_key: keese.Value,
    track_key: Id,
    is_random: bool,
};

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
            log.warn("overwriting existing queue item: {}", item_id);
        }
        gop.value_ptr.* = .{
            .sort_key = sort_key,
            .track_key = library_key,
            .is_random = false,
        };
    }
    current_queue_version = Id.random();
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
    current_queue_version = Id.random();
    try subscriptions.broadcastChanges(arena, .queue);
}

pub fn remove(arena: Allocator, args: []Id) !void {
    for (args) |item_id| {
        if (!items.swapRemove(item_id)) {
            log.warn("attempt to remove non-existent item: {}", .{item_id});
        }
    }
    current_queue_version = Id.random();
    try subscriptions.broadcastChanges(arena, .queue);
}

pub fn getSerializable(arena: std.mem.Allocator) !IdMap(groovebasin_protocol.QueueItem) {
    var result = IdMap(groovebasin_protocol.QueueItem){};

    var it = items.iterator();
    while (it.next()) |kv| {
        const id = kv.key_ptr.*;
        const queue_item = kv.value_ptr.*;
        try result.map.putNoClobber(arena, id, itemToSerializedForm(queue_item));
    }
    return result;
}

fn itemToSerializedForm(item: InternalQueueItem) groovebasin_protocol.QueueItem {
    return .{
        .sortKey = item.sort_key,
        .key = item.track_key,
        .isRandom = item.is_random,
    };
}
