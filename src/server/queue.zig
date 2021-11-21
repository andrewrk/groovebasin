const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;

const g = @import("global.zig");

const protocol = @import("shared").protocol;
const QueueItem = protocol.QueueItem;

const Queue = @import("shared").Queue;

pub var current_queue_version: u64 = 1;
pub var queue: Queue = undefined;

pub fn init() !void {
    queue = Queue{
        .items = AutoArrayHashMap(u64, QueueItem).init(g.gpa),
    };
    errdefer queue.deinit();
}

pub fn deinit() void {
    queue.deinit();
}

var next_item_key: u64 = 10;
var next_sort_key: u64 = 1;

pub fn generateItemKey() u64 {
    defer {
        next_item_key += 1;
    }
    return next_item_key;
}

pub fn generateSortKey() u64 {
    defer {
        next_sort_key += 1_000_000_000;
    }
    return next_sort_key;
}
