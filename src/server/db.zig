const std = @import("std");
const Allocator = std.mem.Allocator;
const log = std.log;

const users = @import("users.zig");
const subscriptions = @import("subscriptions.zig");

const Header = extern struct {
    /// This confirms that this is in fact a groovebasin db.
    magic_number: [4]u8 = .{ 0xf6, 0x2e, 0xb5, 0x9a },
    /// Panic with a todo if this isn't right.
    endian_check: u16 = 0x1234,
    /// Bump this during devlopment to signal a breaking change.
    /// This causes existing dbs on old versions to be silently *deleted*.
    dev_version: u16 = 0,
};

const Entry = union(enum) {
    users: struct {},
};

pub fn load(db_path: []const u8) !void {
    const file = std.fs.cwd().openFile(db_path, .{}) catch |e| switch (e) {
        error.FileNotFound => {
            log.warn("db not found. starting with an empty database: {s}", .{db_path});
            return createEmptyDatabase(db_path);
        },
        else => |err| return err,
    };
    defer file.close();

    const found_header = try file.reader().readStruct(Header);
    if (!std.mem.eql(u8, &found_header.magic_number, &(Header{}).magic_number)) return error.NotAGrooveBasinDb;
    if (found_header.endian_check != (Header{}).endian_check) @panic("TODO: consider endianness");
    if (found_header.dev_version != (Header{}).dev_version) {
        log.warn("found db from older dev version: {} (current: {}). deleting database.", .{
            found_header.dev_version,
            (Header{}).dev_version,
        });
        return createEmptyDatabase(db_path);
    }

    // TODO actually read
}

pub fn createEmptyDatabase(db_path: []const u8) !void {
    _ = db_path;
    // TODO
}

const SubscriptionTag = std.meta.Tag(@import("groovebasin_protocol.zig").Subscription);
const SubscriptionBoolArray = [std.enums.directEnumArrayLen(SubscriptionTag, 0)]bool;
const subscription_bool_array_initial_value = std.enums.directEnumArrayDefault(SubscriptionTag, bool, false, 0, .{});

pub const Changes = struct {
    arena: Allocator,
    clients_to_notify_for_self_user_info: std.AutoArrayHashMapUnmanaged(*anyopaque, void) = .{},
    subscriptions_to_broadcast: SubscriptionBoolArray = subscription_bool_array_initial_value,

    pub fn init(arena: Allocator) @This() {
        return .{
            .arena = arena,
        };
    }
    pub fn deinit(self: *@This()) void {
        self.clients_to_notify_for_self_user_info.deinit(self.arena);
        self.* = undefined;
    }

    pub fn flush(self: *@This()) error{OutOfMemory}!void {
        for (self.clients_to_notify_for_self_user_info.keys()) |client_id| {
            try users.sendSelfUserInfo(client_id);
        }
        for (self.subscriptions_to_broadcast, 0..) |should_broadcast, i| {
            if (!should_broadcast) continue;
            const name: SubscriptionTag = @enumFromInt(i);
            try subscriptions.broadcastChanges(self.arena, name);
        }
    }

    pub fn sendSelfUserInfo(self: *@This(), client_id: *anyopaque) error{OutOfMemory}!void {
        _ = try self.clients_to_notify_for_self_user_info.put(self.arena, client_id, {});
    }

    pub fn broadcastChanges(self: *@This(), name: SubscriptionTag) void {
        self.subscriptions_to_broadcast[@intFromEnum(name)] = true;
    }
};
