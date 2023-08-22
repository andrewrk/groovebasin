const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;
const log = std.log;

const g = @import("global.zig");

const StringPool = @import("shared").StringPool;
const subscriptions = @import("subscriptions.zig");

const Id = @import("groovebasin_protocol.zig").Id;
const IdMap = @import("groovebasin_protocol.zig").IdMap;
const Permissions = @import("groovebasin_protocol.zig").Permissions;
const PublicUserInfo = @import("groovebasin_protocol.zig").PublicUserInfo;

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

const Session = struct {
    user_id: Id,
    claims_to_be_streaming: bool = false,
};

pub var current_version: Id = undefined;
var strings: StringPool = undefined;
var user_accounts: AutoArrayHashMap(Id, UserAccount) = undefined;
var sessions: AutoArrayHashMap(*anyopaque, Session) = undefined;

pub fn init() !void {
    current_version = Id.random();
    user_accounts = AutoArrayHashMap(Id, UserAccount).init(g.gpa);
    strings = StringPool.init(g.gpa);
}
pub fn deinit() void {
    user_accounts.deinit();
    strings.deinit();
}

pub fn haveAdminUser() bool {
    for (user_accounts.values()) |*user| {
        if (user.perms.admin) return true;
    }
    return false;
}

pub fn ensureAdminUser(arena: Allocator) !void {
    if (haveAdminUser()) return;

    var name_str: ["Admin-123456".len]u8 = "Admin-XXXXXX".*;
    for (name_str[name_str.len - 6 ..]) |*c| {
        c.* = std.crypto.random.intRangeAtMost(u8, '0', '9');
    }
    const name = try strings.putWithoutDeduplication(&name_str);
    // TODO: validate uniqueness of username.

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
    try subscriptions.broadcastChanges(arena, .haveAdminUser);
    try subscriptions.broadcastChanges(arena, .users);
}

pub fn getSerializable(arena: Allocator) !IdMap(PublicUserInfo) {
    var result = IdMap(PublicUserInfo){};
    try result.map.ensureTotalCapacity(arena, user_accounts.count());

    var it = user_accounts.iterator();
    while (it.next()) |kv| {
        const user_id = kv.key_ptr.*;
        const account = kv.value_ptr;
        result.map.putAssumeCapacityNoClobber(user_id, .{
            .name = strings.getString(account.name),
            .perms = account.perms,
            .requested = account.requested,
            .connected = false,
            .streaming = false,
        });
    }

    for (sessions.values()) |*session| {
        var public_info = result.map.getEntry(session.user_id).?.value_ptr;
        public_info.connected = true;
        if (session.claims_to_be_streaming) {
            public_info.streaming = true;
        }
    }

    return result;
}
