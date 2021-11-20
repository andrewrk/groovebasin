const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;

const QueueItem = @import("protocol.zig").QueueItem;

pub const Queue = struct {
    items: AutoArrayHashMap(u64, QueueItem),

    pub fn deinit(self: *@This()) void {
        self.items.deinit();
        self.* = undefined;
    }
};
