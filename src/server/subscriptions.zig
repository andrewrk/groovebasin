const std = @import("std");
const ArrayList = std.ArrayList;
const Allocator = std.mem.Allocator;
const Tag = std.meta.Tag;

const g = @import("global.zig");
const library = @import("library.zig");
const queue = @import("queue.zig");
const events = @import("events.zig");
const encodeAndSend = @import("server_main.zig").encodeAndSend;
const users = @import("users.zig");

const Subscription = @import("groovebasin_protocol.zig").Subscription;
const Id = @import("groovebasin_protocol.zig").Id;

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
    const client_data = try client_subscriptions.addOne();
    errdefer {
        _ = client_subscriptions.pop();
    }
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
    var version: ?Id = null;
    var sub: Subscription = switch (client_data.name) {
        .sessions => .{ .sessions = try users.getSerializableSessions(arena, &version) },
        .users => .{ .users = try users.getSerializableUsers(arena, &version) },
        .streamEndpoint => .{ .streamEndpoint = "stream.mp3" },
        .library => .{ .library = try library.getSerializable(arena, &version) },
        .queue => .{ .queue = try queue.getSerializable(arena, &version) },
        .events => .{ .events = try events.getSerializable(arena, &version) },
        .currentTrack => .{ .currentTrack = queue.getSerializedCurrentTrack() },
        else => return, // TODO: support more subscription streams.
    };

    // Wrap in the appropriate structure wrt delta versioning.
    // TODO: actually compute a delta instead of full resetting on every change.
    if (client_data.delta) {
        if (version == null) return error.BadRequest; // Delta not available for this subscription.
        if (client_data.last_version) |v| {
            if (v.value == version.?.value) return; // Up to date.
        }
        try encodeAndSend(client_data.client_id, .{ .subscription = .{
            .sub = sub,
            .delta_version = version.?,
        } });
    } else {
        try encodeAndSend(client_data.client_id, .{ .subscription = .{ .sub = sub } });
    }
}
