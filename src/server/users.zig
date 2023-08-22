const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;
const log = std.log;

const g = @import("global.zig");

const StringPool = @import("shared").StringPool;
const Id = @import("groovebasin_protocol.zig").Id;
const Permissions = @import("groovebasin_protocol.zig").Permissions;
const subscriptions = @import("subscriptions.zig");

const UserAccount = struct {
    name: u32,
    password_hash: PasswordHash,
    registered: bool,
    requested: bool,
    approved: bool,
    perms: Permissions,
};

/// Uses sha256
const PasswordHash = struct {
    salt: [16]u8,
    hash: [32]u8,
};

pub var current_version: Id = undefined;
var user_accounts: AutoArrayHashMap(Id, UserAccount) = undefined;
var strings: StringPool = undefined;

pub fn init() !void {
    current_version = Id.random();
    user_accounts = AutoArrayHashMap(Id, UserAccount).init(g.gpa);
    strings = StringPool.init(g.gpa);
}
pub fn deinit() void {
    user_accounts.deinit();
    strings.deinit();
}

pub fn ensureAdminUser(arena: Allocator) !void {
    // TODO: check if there's one already.

    var name_str: ["Admin-123456".len]u8 = "Admin-XXXXXX".*;
    for (name_str[name_str.len - 6 ..]) |*c| {
        c.* = std.crypto.random.intRangeAtMost(u8, '0', '9');
    }
    const name = try strings.putWithoutDeduplication(&name_str);

    var password_hash: PasswordHash = undefined;
    std.crypto.random.bytes(&password_hash.salt);
    var password_text: [16]u8 = undefined;
    for (&password_text) |*c| {
        c.* = std.crypto.random.intRangeAtMost(u8, ' ' + 1, '~');
    }
    {
        var h = std.crypto.hash.sha2.Sha256.init(.{});
        h.update(&password_text);
        h.final(&password_hash.hash);
    }

    const gop = try user_accounts.getOrPut(Id.random());
    if (gop.found_existing) @panic("unlikely"); // TODO: use generateIdAndPut() kinda thing.
    const user = gop.value_ptr;
    user.* = UserAccount{
        .name = name,
        .password_hash = password_hash,
        .registered = true,
        .requested = true,
        .approved = true,
        .perms = .{
            .read = true,
            .add = true,
            .control = true,
            .playlist = true,
            .admin = true,
        },
    };

    log.info("No admin account found. Created one:", .{});
    log.info("Username: {s}", .{name_str[0..]});
    log.info("Password: {s}", .{password_text[0..]});

    current_version = Id.random();
    try subscriptions.broadcastChanges(arena, .users);
}
