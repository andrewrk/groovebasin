const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;
const log = std.log;

const g = @import("global.zig");

const groovebasin_protocol = @import("groovebasin_protocol.zig");
const Id = @import("groovebasin_protocol.zig").Id;
const IdMap = @import("groovebasin_protocol.zig").IdMap;
const keese = @import("keese.zig");

pub var current_queue_version: u64 = 1;
var items: AutoArrayHashMap(Id, InternalQueueItem) = undefined;

pub fn init() !void {
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

pub fn enqueue(new_items: anytype) !void {
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
