const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;

const Id = @import("groovebasin_protocol.zig").Id;
const g = @import("global.zig");

pub const Event = extern struct {
    sort_key: u64, // TODO: switch to a keese string.
    // these are all chat messages.
    name: u32,
    content: u32,
};

pub const Events = @import("shared").Events;
pub const StringPool = @import("shared").StringPool;

pub var current_events_version: u64 = 1;
var events: AutoArrayHashMap(u64, Event) = undefined;
var strings: StringPool = undefined;
var events_string_putter: StringPool.Putter = undefined;

pub fn init() !void {
    strings = StringPool.init(g.gpa);
    events = AutoArrayHashMap(u64, Event).init(g.gpa);
    errdefer events.deinit();

    events_string_putter = strings.initPutter();
    errdefer events_string_putter.deinit();
}

pub fn deinit() void {
    strings.deinit();
    events.deinit();
}

pub fn revealTrueIdentity(guest_id: Id, real_id: Id) !void {
    _ = guest_id;
    _ = real_id;
    // TODO
}

pub fn tombstoneUser(user_id: Id) !void {
    _ = user_id;
    // TODO
}
