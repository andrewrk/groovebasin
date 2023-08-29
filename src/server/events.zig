const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;

const Id = @import("groovebasin_protocol.zig").Id;
const IdMap = @import("groovebasin_protocol.zig").IdMap;
const Event = @import("groovebasin_protocol.zig").Event;
const EventUserId = @import("groovebasin_protocol.zig").EventUserId;
const Datetime = @import("groovebasin_protocol.zig").Datetime;

const g = @import("global.zig");
const keese = @import("keese.zig");
const subscriptions = @import("subscriptions.zig");
const getNow = @import("server_main.zig").getNow;
const users = @import("users.zig");
const db = @import("db.zig");

pub const InternalEvent = struct {
    date: Datetime,
    // TODO: we really don't need this. just sort on date.
    sort_key: keese.Value,
    who: EventUserId,
    type: union(enum) {
        chat: struct {
            text: StringPool.Index,
            is_slash_me: bool,
        },
    },
};

const StringPool = @import("StringPool.zig");

var current_version: Id = undefined;
var strings: StringPool = undefined;
var events: AutoArrayHashMap(Id, InternalEvent) = undefined;

var deleted_text: StringPool.Index = undefined;

pub fn init() !void {
    current_version = Id.random();
    strings = StringPool.init(g.gpa);
    events = AutoArrayHashMap(Id, InternalEvent).init(g.gpa);

    deleted_text = try strings.putWithoutDeduplication("<deleted>");
}

pub fn deinit() void {
    strings.deinit();
    events.deinit();
}

pub fn chat(arena: Allocator, user_id: Id, text: []const u8, is_slash_me: bool) !void {
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
        .who = .{ .user = user_id },
        .type = .{
            .chat = .{
                .text = strings.putWithoutDeduplicationAssumeCapacity(text),
                .is_slash_me = is_slash_me,
            },
        },
    };

    current_version = Id.random();
    try subscriptions.broadcastChanges(arena, .events);
}

pub fn revealTrueIdentity(changes: *db.Changes, guest_id: Id, real_id: Id) !void {
    for (events.values()) |*event| {
        if (event.who == .user and event.who.user.value == guest_id.value) {
            event.who = .{ .user = real_id };
            current_version = Id.random();
        }
    }
    changes.broadcastChanges(.events);
}

pub fn tombstoneUser(changes: *db.Changes, user_id: Id) !void {
    for (events.values()) |*event| {
        if (event.who == .user and event.who.user.value == user_id.value) {
            event.who = .deleted_user;
            switch (event.type) {
                .chat => |*data| {
                    data.* = .{
                        .text = deleted_text,
                        // make it italics or something:
                        .is_slash_me = true,
                        // (this is still not unambiguous)
                    };
                },
            }
            current_version = Id.random();
        }
    }
    changes.broadcastChanges(.events);
}

pub fn getSerializable(arena: Allocator, out_version: *?Id) !IdMap(Event) {
    out_version.* = current_version;
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
                .userId = event.who,
                .text = strings.getString(data.text),
                .displayClass = if (data.is_slash_me) .me else null,
            },
        });
    }

    return result;
}
