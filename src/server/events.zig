const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;

const Id = @import("groovebasin_protocol.zig").Id;
const IdMap = @import("groovebasin_protocol.zig").IdMap;
const Event = @import("groovebasin_protocol.zig").Event;
const Datetime = @import("groovebasin_protocol.zig").Datetime;

const g = @import("global.zig");
const keese = @import("keese.zig");
const subscriptions = @import("subscriptions.zig");
const getNow = @import("server_main.zig").getNow;
const users = @import("users.zig");

pub const InternalEvent = struct {
    date: Datetime,
    // TODO: we really don't need this. just sort on date.
    sort_key: keese.Value,
    type: union(enum) {
        chat: struct {
            text: u32, // in strings.
            user_id: ?Id, // null for a tombstoned user.
            is_slash_me: bool,
        },
    },
};

pub const StringPool = @import("shared").StringPool;

pub var current_version: Id = undefined;
var strings: StringPool = undefined;
var events: AutoArrayHashMap(Id, InternalEvent) = undefined;

pub fn init() !void {
    current_version = Id.random();
    strings = StringPool.init(g.gpa);
    events = AutoArrayHashMap(Id, InternalEvent).init(g.gpa);
}

pub fn deinit() void {
    strings.deinit();
    events.deinit();
}

pub fn chat(arena: Allocator, client_id: *anyopaque, text: []const u8, is_slash_me: bool) !void {
    const user_id = users.userIdFromClientId(client_id);
    try strings.ensureUnusedCapacity(text.len);
    try events.ensureUnusedCapacity(1);
    const sort_key = if (events.count() == 0)
        keese.starting_value
    else
        try keese.above(events.values()[events.count() - 1].sort_key);

    const event_id = Id.random();
    const gop = events.getOrPutAssumeCapacity(event_id);
    if (gop.found_existing) @panic("unlikely"); // TODO: use generateIdAndPut() kinda thing.
    gop.key_ptr.* = event_id;
    gop.value_ptr.* = InternalEvent{
        .date = getNow(),
        .sort_key = sort_key,
        .type = .{
            .chat = .{
                .user_id = user_id,
                .text = strings.putWithoutDeduplicationAssumeCapacity(text),
                .is_slash_me = is_slash_me,
            },
        },
    };

    try subscriptions.broadcastChanges(arena, .events);
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

pub fn getSerializable(arena: Allocator) !IdMap(Event) {
    var result: IdMap(Event) = .{};
    try result.map.ensureUnusedCapacity(arena, events.count());

    var it = events.iterator();
    while (it.next()) |kv| {
        const event = kv.value_ptr;
        result.map.putAssumeCapacityNoClobber(kv.key_ptr.*, switch (event.type) {
            .chat => |data| .{
                .date = event.date,
                .sortKey = event.sort_key,
                .type = .chat,
                .userId = data.user_id,
                .text = strings.getString(data.text),
                .displayClass = if (data.is_slash_me) .me else null,
            },
        });
    }

    return result;
}
