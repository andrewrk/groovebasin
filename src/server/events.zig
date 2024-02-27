const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;

const Id = @import("groovebasin_protocol.zig").Id;
const IdMap = @import("groovebasin_protocol.zig").IdMap;
const Event = @import("groovebasin_protocol.zig").Event;
const Datetime = @import("groovebasin_protocol.zig").Datetime;

const subscriptions = @import("subscriptions.zig");
const getNow = @import("server_main.zig").getNow;
const users = @import("users.zig");
const db = @import("db.zig");
const InternalEvent = db.InternalEvent;

const StringPool = @import("StringPool.zig");

pub fn init() !void {}
pub fn deinit() void {}

pub fn chat(user_id: Id, text_str: []const u8, is_slash_me: bool) !void {
    const g = @import("global.zig");
    const events = &g.the_database.events;

    const text = try g.strings.put(g.gpa, text_str);
    const sort_key: f64 = if (events.table.count() == 0)
        0.0
    else
        events.table.values()[events.table.count() - 1].sort_key + 1.0;

    _ = try events.putRandom(.{
        .date = getNow(),
        .sort_key = sort_key,
        .who = .{ .user = user_id },
        .type = .{
            .chat = .{
                .text = text,
                .is_slash_me = is_slash_me,
            },
        },
    });
}

pub fn revealTrueIdentity(guest_id: Id, real_id: Id) !void {
    const g = @import("global.zig");
    const events = &g.the_database.events;
    var it = events.iterator();
    while (it.next()) |kv| {
        const event = kv.value_ptr;
        if (event.who == .user and event.who.user.value == guest_id.value) {
            const event_for_editing = try it.promoteForEditing(kv);
            event_for_editing.who = .{ .user = real_id };
        }
    }
}

pub fn tombstoneUser(user_id: Id) !void {
    const g = @import("global.zig");
    const events = &g.the_database.events;
    var it = events.iterator();
    while (it.next()) |kv| {
        const event = kv.value_ptr;
        if (event.who == .user and event.who.user.value == user_id.value) {
            const ev = try it.promoteForEditing(kv);
            ev.who = .deleted_user;
            switch (ev.type) {
                .chat => |*data| {
                    data.* = .{
                        .text = .deleted_placeholder,
                        // make it italics or something:
                        .is_slash_me = true,
                        // (this is still not unambiguous)
                    };
                },
            }
        }
    }
}

pub fn serializableEvent(event: InternalEvent) Event {
    const g = @import("global.zig");
    return switch (event.type) {
        .chat => |data| .{
            .date = event.date,
            .sortKey = event.sort_key,
            .type = .chat,
            .userId = event.who,
            .text = g.strings.get(data.text),
            .displayClass = if (data.is_slash_me) .me else null,
        },
    };
}
