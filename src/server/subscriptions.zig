const std = @import("std");
const ArrayList = std.ArrayList;
const Allocator = std.mem.Allocator;
const Tag = std.meta.Tag;
const log = std.log;
const assert = std.debug.assert;

const g = @import("global.zig");
const sendBytes = @import("server_main.zig").sendBytes;

const db = @import("db.zig");

const protocol = @import("groovebasin_protocol.zig");
const Subscription = protocol.Subscription;
const Id = protocol.Id;

const ClientSubscriptionData = struct {
    client_id: Id,
    name: Tag(Subscription),
    delta: bool,
    last_version: ?Id,
};

var client_subscriptions: ArrayList(ClientSubscriptionData) = undefined;

pub fn init() !void {
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

    var arena = std.heap.ArenaAllocator.init(g.gpa);
    defer arena.deinit();
    try publishData(arena.allocator(), client_data);
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

pub fn broadcastAllChanges(arena: Allocator) !void {
    for (client_subscriptions.items) |*client_data| {
        try publishData(arena, client_data);
    }
}

pub fn broadcastChanges(arena: Allocator, name: Tag(Subscription)) error{OutOfMemory}!void {
    for (client_subscriptions.items) |*item| {
        if (item.name != name) continue;
        publishData(arena, item) catch |err| switch (err) {
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

fn publishData(arena: Allocator, client_data: *ClientSubscriptionData) !void {
    assert(client_data.delta); // TODO
    switch (client_data.name) {
        .sessions => {
            return doTheThing(arena, client_data.client_id, &client_data.last_version, &g.the_database.sessions);
        },
        .users => {}, // TODO,
        .library => {}, // TODO,
        .queue => {}, // TODO,
        .events => {}, // TODO,
        .state => {}, // TODO,

        // TODO: support more subscription streams.
        .playlists,
        .importProgress,
        .anonStreamers,
        .protocolMetadata,
        .labels,
        => return,
    }
}

fn doTheThing(arena: Allocator, client_id: Id, last_version: *?Id, database: anytype) !void {
    _ = arena;
    var reset = true;
    if (last_version.*) |v| {
        if (v.value == database.version.?.value) return; // Up to date.
        if (database.last_version) |last_db_version| {
            if (v.value == last_db_version.value) {
                // Enable delta compression
                reset = false;
                log.debug("client has correct version for {s}", .{@tagName(@TypeOf(database.*).subscription)});
            } else {
                log.debug("client has wrong version for {s}: {}, correct: {}", .{
                    @tagName(@TypeOf(database.*).subscription), v, last_db_version,
                });
            }
        }
    }

    var bytes_list = ArrayList(u8).init(g.gpa);
    defer bytes_list.deinit();
    var json_writer = std.json.writeStream(bytes_list.writer(), .{});
    var jw = &json_writer;

    try jw.beginObject();
    try jw.objectField("name");
    try jw.write(@tagName(@TypeOf(database.*).subscription));
    try jw.objectField("args");

    try jw.beginObject();
    try jw.objectField("version");
    try serialize(jw, database.version);
    try jw.objectField("reset");
    try jw.write(reset);
    try jw.objectField("delta");

    try jw.beginObject();
    if (reset) {
        var it = database.iterator();
        while (it.next()) |kv| {
            var buf: [8]u8 = undefined;
            try jw.objectField(kv.key_ptr.*.write(&buf));
            try serialize(jw, kv.value_ptr.*);
        }
    } else {
        for (database.removed_keys.keys()) |key| {
            var buf: [8]u8 = undefined;
            try jw.objectField(key.write(&buf));
            try jw.write(null);
        }
        for (database.added_keys.keys()) |key| {
            var buf: [8]u8 = undefined;
            try jw.objectField(key.write(&buf));
            try serialize(jw, database.get(key).*);
        }
        var it = database.modified_entries.iterator();
        while (it.next()) |kv| {
            var buf: [8]u8 = undefined;
            try jw.objectField(kv.key_ptr.*.write(&buf));
            try serializeDiff(jw, kv.value_ptr.*, database.get(kv.key_ptr.*).*);
        }
    }
    try jw.endObject();

    try jw.endObject();
    try jw.endObject();

    try sendBytes(client_id, try bytes_list.toOwnedSlice());

    last_version.* = database.version;
}

fn serialize(jw: anytype, value: anytype) !void {
    switch (@TypeOf(value)) {
        Id => {
            var buf: [8]u8 = undefined;
            try jw.write(value.write(&buf));
        },
        ?Id => {
            if (value) |id| {
                try serialize(jw, id);
            } else {
                try jw.write(null);
            }
        },

        db.InternalSession => {
            try jw.beginObject();
            try jw.objectField("userId");
            try serialize(jw, value.user_id);
            try jw.objectField("streaming");
            try jw.write(value.claims_to_be_streaming);
            try jw.endObject();
        },

        else => @compileError("TODO: " ++ @typeName(@TypeOf(value))),
    }
}

fn serializeDiff(jw: anytype, from_value: anytype, to_value: @TypeOf(from_value)) !void {
    switch (@TypeOf(from_value)) {
        db.InternalSession => {
            try jw.beginObject();
            if (from_value.user_id.value != to_value.user_id.value) {
                try jw.objectField("userId");
                try serialize(jw, to_value.user_id);
            }
            if (from_value.claims_to_be_streaming != to_value.claims_to_be_streaming) {
                try jw.objectField("streaming");
                try jw.write(to_value.claims_to_be_streaming);
            }
            try jw.endObject();
        },

        else => @compileError("TODO: " ++ @typeName(@TypeOf(from_value))),
    }
}

fn getSerializableState(out_version: *?Id) protocol.State {
    out_version.* = Id.random();
    return .{
        .currentTrack = undefined, //queue.getSerializedCurrentTrack(),
        .autoDj = .{ // TODO
            .on = false,
            .historySize = 10,
            .futureSize = 10,
        },
        .repeat = .off, // TODO
        .volumePercent = 100, // TODO
        .hardwarePlayback = false, // TODO
        .streamEndpoint = "stream.mp3",
        .guestPermissions = undefined, //users.getSerializableGuestPermissions(),
    };
}
