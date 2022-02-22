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
