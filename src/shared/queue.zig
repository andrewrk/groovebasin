const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;

const QueueItem = @import("protocol.zig").QueueItem;

pub const Queue = struct {
    items: AutoArrayHashMap(u64, QueueItem),

    pub fn init(allocator: Allocator) @This() {
        return @This(){
            .items = AutoArrayHashMap(u64, QueueItem).init(allocator),
        };
    }
    pub fn deinit(self: *@This()) void {
        self.items.deinit();
        self.* = undefined;
    }
};
