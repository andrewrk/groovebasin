const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;

const g = @import("global.zig");

const protocol = @import("shared").protocol;
const Event = protocol.Event;

const Events = @import("shared").Events;
const StringPool = @import("shared").StringPool;

pub var current_events_version: u64 = 1;
pub var events: Events = undefined;

pub fn init() !void {
    events = Events.init(g.gpa);
    errdefer events.deinit();
}

pub fn deinit() void {
    events.deinit();
}
