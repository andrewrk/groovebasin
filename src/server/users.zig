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
    /// Display name and login username.
    name: u32,
    /// Null for freshly generated guest accounts.
    password_hash: ?PasswordHash,
    /// True iff the user has specified the name of this account.
    /// Could be renamed to non_guest.
    registered: bool,
    /// Has the user clicked the Request Approval button?
    requested: bool,
    /// Has an admin clicked the Approve button (after the above request)?
    approved: bool,
    /// Short for permissions, not hairstyling.
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
var guest_perms: Permissions = .{
    // Can be changed by admins.
    .read = true,
    .add = true,
    .control = true,
    .playlist = false,
    .admin = false,
};

pub fn init() !void {
    current_version = Id.random();
    strings = StringPool.init(g.gpa);
    user_accounts = AutoArrayHashMap(Id, UserAccount).init(g.gpa);
    sessions = AutoArrayHashMap(*anyopaque, Session).init(g.gpa);
}
pub fn deinit() void {
    strings.deinit();
    user_accounts.deinit();
    sessions.deinit();
}

pub fn handleClientConnected(arena: Allocator, client_id: *anyopaque) !void {
    // Every new connection starts as a guest.
    // If you want to be not a guest, send a login message.
    const user_id = try createGuestAccount();
    try sessions.putNoClobber(client_id, .{
        .user_id = user_id,
        .claims_to_be_streaming = false,
    });

    try subscriptions.broadcastChanges(arena, .users);
}

pub fn handleClientDisconnected(arena: Allocator, client_id: *anyopaque) !void {
    std.debug.assert(sessions.swapRemove(client_id));
    try subscriptions.broadcastChanges(arena, .users);
}

pub fn setStreaming(arena: Allocator, client_id: *anyopaque, is_streaming: bool) !void {
    sessions.getEntry(client_id).?.value_ptr.claims_to_be_streaming = is_streaming;

    try subscriptions.broadcastChanges(arena, .users);
}

pub fn haveAdminUser() bool {
    for (user_accounts.values()) |*user| {
        if (user.perms.admin) return true;
    }
    return false;
}

fn createGuestAccount() !Id {
    var name_str: ["Guest-123456".len]u8 = "Guest-XXXXXX".*;
    for (name_str[name_str.len - 6 ..]) |*c| {
        c.* = std.base64.url_safe_alphabet_chars[std.crypto.random.int(u6)];
    }
    const name = try strings.putWithoutDeduplication(&name_str);
    // TODO: validate uniqueness of username.

    const gop = try user_accounts.getOrPut(Id.random());
    if (gop.found_existing) @panic("unlikely"); // TODO: use generateIdAndPut() kinda thing.
    const user = gop.value_ptr;
    user.* = UserAccount{
        .name = name,
        .password_hash = null,
        .registered = false,
        .requested = false,
        .approved = false,
        .perms = guest_perms,
    };
    // publishing users event happens one call up.
    return gop.key_ptr.*;
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
