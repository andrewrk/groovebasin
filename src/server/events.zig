const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;

const g = @import("global.zig");

const protocol = @import("shared").protocol;
const Event = protocol.Event;

pub const Events = @import("shared").Events;
pub const StringPool = @import("shared").StringPool;

pub var current_events_version: u64 = 1;
pub var events: Events = undefined;
pub var events_string_putter: StringPool.Putter = undefined;

pub fn init() !void {
    events = Events{
        .strings = StringPool.init(g.gpa),
        .events = AutoArrayHashMap(u64, Event).init(g.gpa),
    };
    errdefer events.deinit();

    events_string_putter = events.strings.initPutter();
    errdefer events_string_putter.deinit();
}

pub fn deinit() void {
    events.deinit();
}
