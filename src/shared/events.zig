const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;

const StringPool = @import("string_pool.zig").StringPool;
const Event = @import("protocol.zig").Event;

pub const Events = struct {
    strings: StringPool,
    events: AutoArrayHashMap(u64, Event),

    pub fn init(allocator: Allocator) @This() {
        return @This(){
            .strings = StringPool.init(allocator),
            .events = AutoArrayHashMap(u64, Event).init(allocator),
        };
    }
    pub fn deinit(self: *@This()) void {
        self.strings.deinit();
        self.events.deinit();
        self.* = undefined;
    }

    pub fn putEvent(self: *@This(), strings: StringPool, event_id: u64, event: Event) !void {
        try self.events.put(
            event_id,
            Event{
                .sort_key = event.sort_key,
                .name = try self.strings.putString(strings.getString(event.name)),
                .content = try self.strings.putString(strings.getString(event.content)),
            },
        );
    }

    pub fn getStringZ(self: @This(), index: u32) [*:0]const u8 {
        return self.strings.getStringZ(index);
    }
    pub fn getString(self: @This(), index: u32) [:0]const u8 {
        return self.strings.getString(index);
    }
};
