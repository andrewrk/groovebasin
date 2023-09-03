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
const db = @import("db.zig");

const StringPool = @import("StringPool.zig");

const events = &g.the_database.events;

pub fn init() !void {}
pub fn deinit() void {}

pub fn chat(changes: *db.Changes, user_id: Id, text_str: []const u8, is_slash_me: bool) !void {
    const text = try g.strings.put(g.gpa, text_str);
    const sort_key = if (events.table.count() == 0)
        keese.starting_value
    else
        try keese.above(events.table.values()[events.table.count() - 1].sort_key);

    _ = try events.putRandom(changes, .{
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

pub fn revealTrueIdentity(changes: *db.Changes, guest_id: Id, real_id: Id) !void {
    var it = events.iterator();
    while (it.next()) |kv| {
        const event = kv.value_ptr;
        if (event.who == .user and event.who.user.value == guest_id.value) {
            it.promoteForEditing(changes, kv).who = .{ .user = real_id };
        }
    }
}

pub fn tombstoneUser(changes: *db.Changes, user_id: Id) !void {
    var it = events.iterator();
    while (it.next()) |kv| {
        const event = kv.value_ptr;
        if (event.who == .user and event.who.user.value == user_id.value) {
            const ev = it.promoteForEditing(changes, kv);
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

pub fn getSerializable(arena: Allocator, out_version: *?Id) !IdMap(Event) {
    out_version.* = Id.random(); // TODO: versioning
    var result: IdMap(Event) = .{};
    try result.map.ensureUnusedCapacity(arena, events.table.count());

    var it = events.iterator();
    while (it.next()) |kv| {
        const event = kv.value_ptr;
        result.map.putAssumeCapacityNoClobber(kv.key_ptr.*, switch (event.type) {
            .chat => |data| .{
                .date = event.date,
                .sortKey = event.sort_key,
                .type = .chat,
                .userId = event.who,
                .text = g.strings.get(data.text),
                .displayClass = if (data.is_slash_me) .me else null,
            },
        });
    }

    return result;
}
