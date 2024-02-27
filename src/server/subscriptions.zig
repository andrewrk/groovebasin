const std = @import("std");
const ArrayList = std.ArrayList;
const Allocator = std.mem.Allocator;
const Tag = std.meta.Tag;
const log = std.log.scoped(.sub);
const assert = std.debug.assert;

const sendBytes = @import("server_main.zig").sendBytes;

const db = @import("db.zig");
const users = @import("users.zig");
const library = @import("library.zig");
const queue = @import("queue.zig");
const events = @import("events.zig");

const protocol = @import("groovebasin_protocol.zig");
const Subscription = protocol.Subscription;
const SubscriptionTag = std.meta.Tag(Subscription);
const Id = protocol.Id;

const ClientSubscriptionData = struct {
    client_id: Id,
    name: Tag(Subscription),
    delta: bool,
    last_version: ?Id,
};

var client_subscriptions: ArrayList(ClientSubscriptionData) = undefined;

pub fn init() !void {
    const g = @import("global.zig");
    client_subscriptions = ArrayList(ClientSubscriptionData).init(g.gpa);
}
pub fn deinit() void {
    client_subscriptions.deinit();
}

pub fn subscribe(client_id: Id, name: Tag(Subscription), delta: bool, version: ?Id) !void {
    if (lookup(client_id, name)) |_| return error.BadRequest; // already subscribed
    if (!delta) {
        log.warn("TODO: ignoring request for non-delta subscription: {}", .{name});
        return;
    }
    const client_data = try client_subscriptions.addOne();
    client_data.* = .{
        .client_id = client_id,
        .name = name,
        .delta = delta,
        .last_version = version,
    };

    try publishData(client_data);
}

pub fn handleClientDisconnected(client_id: Id) void {
    var i = client_subscriptions.items.len;
    while (i > 0) : (i -= 1) {
        const item = &client_subscriptions.items[i - 1];
        if (item.client_id.value == client_id.value) {
            _ = client_subscriptions.swapRemove(i - 1);
        }
    }
}

pub fn broadcastAllChanges() !void {
    for (client_subscriptions.items) |*client_data| {
        try publishData(client_data);
    }
}

pub fn broadcastChanges(name: Tag(Subscription)) error{OutOfMemory}!void {
    for (client_subscriptions.items) |*item| {
        if (item.name != name) continue;
        publishData(item) catch |err| switch (err) {
            error.BadRequest => unreachable,
            error.OutOfMemory => |e| return e,
        };
    }
}

fn lookup(client_id: Id, name: Tag(Subscription)) ?*ClientSubscriptionData {
    for (client_subscriptions.items) |*item| {
        if (item.client_id.value == client_id.value and item.name == name) return item;
    }
    return null;
}

fn publishData(client_data: *ClientSubscriptionData) !void {
    const g = @import("global.zig");
    switch (client_data.name) {
        .sessions => try doTheThing(client_data, &g.the_database.sessions),
        .users => try doTheThing(client_data, &g.the_database.user_accounts),
        .library => try doTheThing(client_data, &g.the_database.tracks),
        .queue => try doTheThing(client_data, &g.the_database.items),
        .events => try doTheThing(client_data, &g.the_database.events),

        // TODO: support more subscription streams.
        .playlists,
        .importProgress,
        .anonStreamers,
        .protocolMetadata,
        .labels,
        => {},

        .state => try doTheStateThing(client_data),
    }
}

fn doTheThing(client_data: *ClientSubscriptionData, database: anytype) !void {
    const g = @import("global.zig");
    var bytes_list = ArrayList(u8).init(g.gpa);
    defer bytes_list.deinit();
    var json_writer = std.json.writeStream(bytes_list.writer(), .{});
    var jw = &json_writer;

    switch (try writeSubscriptionHeader(
        jw,
        client_data,
        @TypeOf(database.*).subscription,
        database.version,
        database.last_version,
    )) {
        .skip => return,
        .full => {
            try jw.beginObject();
            var it = database.iterator();
            while (it.next()) |kv| {
                var buf: [8]u8 = undefined;
                try jw.objectField(kv.key_ptr.*.write(&buf));
                try jw.write(toSerializable(kv.value_ptr.*));
            }
            try jw.endObject();
        },
        .delta => {
            try jw.beginObject();
            for (database.removed_keys.keys()) |key| {
                var buf: [8]u8 = undefined;
                try jw.objectField(key.write(&buf));
                try jw.write(null);
            }
            for (database.added_keys.keys()) |key| {
                var buf: [8]u8 = undefined;
                try jw.objectField(key.write(&buf));
                try jw.write(toSerializable(database.get(key).*));
            }
            var it = database.modified_entries.iterator();
            while (it.next()) |kv| {
                var buf: [8]u8 = undefined;
                try jw.objectField(kv.key_ptr.*.write(&buf));
                try serializeDiff(jw, toSerializable(kv.value_ptr.*), toSerializable(database.get(kv.key_ptr.*).*));
            }
            try jw.endObject();
        },
    }

    try writeSubscriptionFooter(jw, client_data);

    try sendBytes(client_data.client_id, try bytes_list.toOwnedSlice());

    client_data.last_version = database.version;
}

fn doTheStateThing(client_data: *ClientSubscriptionData) !void {
    const g = @import("global.zig");
    var bytes_list = ArrayList(u8).init(g.gpa);
    defer bytes_list.deinit();
    var json_writer = std.json.writeStream(bytes_list.writer(), .{});
    var jw = &json_writer;

    switch (try writeSubscriptionHeader(
        jw,
        client_data,
        .state,
        g.the_database.state_version,
        g.the_database.previous_state_version,
    )) {
        .skip => return,
        .full => {
            try jw.write(serializableState(g.the_database.state));
        },
        .delta => {
            try serializeDiff(
                jw,
                serializableState(g.the_database.previous_state),
                serializableState(g.the_database.state),
            );
        },
    }

    try writeSubscriptionFooter(jw, client_data);

    try sendBytes(client_data.client_id, try bytes_list.toOwnedSlice());

    client_data.last_version = g.the_database.state_version;
}

/// returns iff this is a reset.
fn writeSubscriptionHeader(
    jw: anytype,
    client_data: *ClientSubscriptionData,
    comptime subscription: SubscriptionTag,
    version: Id,
    last_version: Id,
) !enum { skip, delta, full } {
    var reset = true;
    // TODO: support non-delta
    if (client_data.last_version) |v| {
        if (v.value == version.value) return .skip; // Up to date.
        if (v.value == last_version.value) {
            // Enable delta compression
            reset = false;
            log.debug("client has correct version for {s}", .{@tagName(subscription)});
        } else {
            log.debug("client has wrong version for {s}: {}, correct: {}", .{
                @tagName(subscription), v, last_version,
            });
        }
    }

    try jw.beginObject();
    try jw.objectField("name");
    try jw.write(subscription);
    try jw.objectField("args");

    try jw.beginObject();
    try jw.objectField("version");
    try jw.write(version);
    try jw.objectField("reset");
    try jw.write(reset);
    try jw.objectField("delta");

    return if (reset) .full else .delta;
}

fn writeSubscriptionFooter(jw: anytype, client_data: *ClientSubscriptionData) !void {
    _ = client_data; // TODO support non-delta
    try jw.endObject();
    try jw.endObject();
}

fn serializeDiff(jw: anytype, from_value: anytype, value: @TypeOf(from_value)) !void {
    switch (@TypeOf(value)) {
        // Don't run the diffing algorithm on these leaf nodes.
        bool,
        u8,
        u10,
        u15,
        f64,
        Id,
        protocol.EventUserId,
        protocol.Datetime,
        []const u8,
        => return jw.write(value),

        else => {},
    }

    switch (@typeInfo(@TypeOf(value))) {
        // Consider all enums primivive.
        .Enum => return jw.write(value),

        .Optional => {
            if (from_value == null) {
                return jw.write(value);
            }
            if (value == null) {
                return jw.write(null);
            }
            // We only get here when the values are different, so they must both be non-null.
            return serializeDiff(jw, from_value.?, value.?);
        },

        // Nested structs.
        .Struct => |struct_info| {
            if (comptime std.meta.hasFn(@TypeOf(value), "jsonStringify")) {
                @compileError("Add this type to the leaf nodes above: " ++ @typeName(@TypeOf(value)));
            }
            try jw.beginObject();
            inline for (struct_info.fields) |field| {
                if (!deepEquals(@field(from_value, field.name), @field(value, field.name))) {
                    try jw.objectField(field.name);
                    try serializeDiff(jw, @field(from_value, field.name), @field(value, field.name));
                }
            }
            try jw.endObject();
        },

        // This switch is intentionally neglecting certain types we don't want
        // to serialize. For example .Union and .Pointer are intentionally
        // omitted.

        else => @compileError("TODO: " ++ @typeName(@TypeOf(value))),
    }
}

/// The purpose of this is to see if serializing the type to JSON would produce
/// the same representation.
fn deepEquals(a: anytype, b: @TypeOf(a)) bool {
    switch (@TypeOf(a)) {
        // These are either comptime constants or deduplicated string pool references.
        []const u8 => return a.ptr == b.ptr and a.len == b.len,

        else => {},
    }

    switch (@typeInfo(@TypeOf(a))) {
        .Void => return true,
        .Bool, .Int, .Float, .Enum => return a == b,
        .Optional => {
            if (a) |_| {
                if (b == null) return false;
                return deepEquals(a.?, b.?);
            } else {
                return b == null;
            }
        },

        .Struct => |struct_info| {
            inline for (struct_info.fields) |field| {
                if (!deepEquals(@field(a, field.name), @field(b, field.name))) return false;
            }
            return true;
        },

        .Union => |union_info| {
            const TagType = union_info.tag_type.?;
            if (@as(TagType, a) != @as(TagType, b)) return false;
            switch (a) {
                inline else => |a_value, tag| {
                    if (!deepEquals(a_value, @field(b, @tagName(tag)))) return false;
                },
            }
            return true;
        },

        // This switch is intentionally neglecting certain types we don't want
        // to compare. For example .Pointer is intentionally omitted.

        else => @compileError("can't deepEquals for type: " ++ @typeName(@TypeOf(a))),
    }
}

fn ToSerializable(comptime T: type) type {
    return switch (T) {
        db.InternalSession => protocol.Session,
        db.UserAccount => protocol.PublicUserInfo,
        db.Track => protocol.LibraryTrack,
        db.Item => protocol.QueueItem,
        db.InternalEvent => protocol.Event,
        else => @compileError("unhandled: " ++ @typeName(T)),
    };
}
fn toSerializable(value: anytype) ToSerializable(@TypeOf(value)) {
    return switch (@TypeOf(value)) {
        db.InternalSession => users.serializableSession(value),
        db.UserAccount => users.serializableUserAccount(value),
        db.Track => library.serializableTrack(value),
        db.Item => queue.serializableItem(value),
        db.InternalEvent => events.serializableEvent(value),
        else => @compileError("unhandled: " ++ @typeName(@TypeOf(value))),
    };
}

fn serializableState(state: db.State) protocol.State {
    return .{
        .currentTrack = queue.serializableCurrentTrack(state.current_item),
        .autoDj = .{
            .on = state.auto_dj.on,
            .historySize = state.auto_dj.history_size,
            .futureSize = state.auto_dj.future_size,
        },
        .repeat = state.repeat,
        .volumePercent = state.volume_percent,
        .hardwarePlayback = state.hardware_playback,
        .streamEndpoint = "stream.mp3",
        .guestPermissions = users.convertPermsissions(state.guest_permissions),
    };
}
