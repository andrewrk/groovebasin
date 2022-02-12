const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;

const StringPool = @import("string_pool.zig").StringPool;
const Event = @import("protocol.zig").Event;

pub const Events = struct {
    strings: StringPool,
    events: AutoArrayHashMap(u64, Event),

    /// The returned slice is invalidated when any strings are added to the string table.
    pub fn getString(self: @This(), index: u32) [:0]const u8 {
        const bytes = self.strings.strings.items;
        var end: usize = index;
        while (bytes[end] != 0) end += 1;
        return bytes[index..end :0];
    }

    pub fn deinit(self: *@This()) void {
        self.strings.strings.deinit();
        self.events.deinit();
        self.* = undefined;
    }
};
