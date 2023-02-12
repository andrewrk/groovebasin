const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;

const StringPool = @import("StringPool.zig");
const Event = @import("protocol.zig").Event;

pub const Events = struct {
    strings: StringPool,
    events: AutoArrayHashMap(u64, Event),

    pub fn deinit(self: *@This()) void {
        self.strings.deinit();
        self.events.deinit();
        self.* = undefined;
    }

    pub fn getString(self: @This(), i: u32) [:0]const u8 {
        return self.strings.getString(i);
    }
};
