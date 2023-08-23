const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;
const log = std.log;

const g = @import("global.zig");

const StringPool = @import("shared").StringPool;
const subscriptions = @import("subscriptions.zig");
const events = @import("events.zig");
const encodeAndSend = @import("server_main.zig").encodeAndSend;

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
var username_to_user_id: std.ArrayHashMapUnmanaged(u32, Id, StringsContext, true) = .{};
var sessions: AutoArrayHashMap(*anyopaque, Session) = undefined;
var guest_perms: Permissions = .{
    // Can be changed by admins.
    .read = true,
    .add = true,
    .control = true,
    .playlist = false,
    .admin = false,
};

const StringsContext = struct {
    pub fn hash(_: @This(), k: u32) u32 {
        return @truncate(std.hash.Wyhash.hash(0, strings.getString(k)));
    }
    pub fn eql(_: @This(), a: u32, b: u32, _: usize) bool {
        return std.mem.eql(u8, strings.getString(a), strings.getString(b));
    }
};
const StringsAdaptedContext = struct {
    pub fn hash(_: @This(), k: []const u8) u32 {
        return @truncate(std.hash.Wyhash.hash(0, k));
    }
    pub fn eql(_: @This(), a: []const u8, b: u32, _: usize) bool {
        return std.mem.eql(u8, a, strings.getString(b));
    }
};

pub fn init() !void {
    current_version = Id.random();
    strings = StringPool.init(g.gpa);
    user_accounts = AutoArrayHashMap(Id, UserAccount).init(g.gpa);
    sessions = AutoArrayHashMap(*anyopaque, Session).init(g.gpa);
}
pub fn deinit() void {
    username_to_user_id.deinit(g.gpa);
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

    try sendSelfUserInfo(client_id);
    try subscriptions.broadcastChanges(arena, .users);
}

pub fn handleClientDisconnected(arena: Allocator, client_id: *anyopaque) !void {
    std.debug.assert(sessions.swapRemove(client_id));
    try subscriptions.broadcastChanges(arena, .users);
}

pub fn login(arena: Allocator, client_id: *anyopaque, username: []const u8, password: []u8) !void {
    defer shred(password);
    var additional_sessions_to_notify = std.ArrayList(*anyopaque).init(arena);
    loginImpl(arena, client_id, username, password, &additional_sessions_to_notify) catch |err| {
        if (err == error.InvalidLogin) {
            // TODO: send the user an error?
        } else return err;
    };
    try sendSelfUserInfo(client_id);
    for (additional_sessions_to_notify.items) |additional_client_id| {
        try sendSelfUserInfo(additional_client_id);
    }
}
fn loginImpl(
    arena: Allocator,
    client_id: *anyopaque,
    username: []const u8,
    password: []u8,
    additional_sessions_to_notify: *std.ArrayList(*anyopaque),
) !void {
    const session = sessions.getEntry(client_id).?.value_ptr;
    const session_account = user_accounts.getEntry(session.user_id).?.value_ptr;

    // We always need a password.
    const min_password_len = 1; // lmao
    if (password.len < min_password_len) return error.PasswordTooShort;

    // These things can happen here:
    // 1. session username == given username
    //     => Change password. Allowed for guest accounts.
    //        Does not count as registering the account.
    // 2. given username is unused
    //     => Change username, change password, and register account as non-guest.
    // 3. given username exists and session is also registered
    //     => Check password to switch login to the given account.
    // 4. given username exists and session is an unregistered guest account
    //     => Check password to login to the given account,
    //        revise history of activity to reveal that the unregistered user_id
    //        was really this existing user_id all along,
    //        then delete the unregistered user_id from the db,
    //        leaving no trace that it ever existed.
    // Perhaps these should have been different APIs.

    if (std.mem.eql(u8, username, strings.getString(session_account.name))) {
        // If you're trying to login to yourself, it always works.
        // Use this to change your password.
        changePassword(session_account, password);
        return;
    }

    if (username_to_user_id.getAdapted(username, StringsAdaptedContext{})) |target_user_id| {
        // Login check.
        const target_account = user_accounts.getEntry(target_user_id).?.value_ptr;
        try checkPassword(target_account, password);
        // You're in.
        if (session_account.registered) {
            // Switch login.
            session.user_id = target_user_id;
        } else {
            // An actual regular login, which is the most complex case.
            const guest_user_id = session.user_id;
            // We're about to delete this user, so make sure all sessions get upgraded.
            var it = sessions.iterator();
            while (it.next()) |kv| {
                if (kv.value_ptr.user_id.value == guest_user_id.value) {
                    // One of these is the session we're dealing with.
                    kv.value_ptr.user_id = target_user_id;
                    if (kv.key_ptr.* != client_id) {
                        try additional_sessions_to_notify.append(kv.key_ptr.*);
                    }
                }
            }
            try events.revealTrueIdentity(guest_user_id, target_user_id);
            deleteAccount(guest_user_id);
        }
    } else {
        // Create account: change username, change password, and register.
        try changeUserName(session.user_id, username);
        changePassword(session_account, password);
        session_account.registered = true;
    }

    try subscriptions.broadcastChanges(arena, .users);
}

fn shred(sensitive: []u8) void {
    // TOO MUCH SECURITY
    std.crypto.random.bytes(sensitive);
}

fn changePassword(account: *UserAccount, new_password: []const u8) void {
    var password_hash: PasswordHash = undefined;
    std.crypto.random.bytes(&password_hash.salt);

    var h = std.crypto.hash.sha2.Sha256.init(.{});
    h.update(&password_hash.salt);
    h.update(new_password);
    h.final(&password_hash.hash);

    account.password_hash = password_hash;
}

fn checkPassword(account: *const UserAccount, password: []const u8) error{InvalidLogin}!void {
    if (account.password_hash == null) return error.InvalidLogin; // That's someone else's freshly created guest account.

    var h = std.crypto.hash.sha2.Sha256.init(.{});
    h.update(&account.password_hash.?.salt);
    h.update(password);
    var hash: [32]u8 = undefined;
    h.final(&hash);

    if (!std.mem.eql(u8, &hash, &account.password_hash.?.hash)) return error.InvalidLogin;
}

fn changeUserName(user_id: Id, new_username: []const u8) !void {
    const account = user_accounts.getEntry(user_id).?.value_ptr;
    const by_name_gop = try username_to_user_id.getOrPutContextAdapted(g.gpa, new_username, StringsAdaptedContext{}, StringsContext{});
    errdefer username_to_user_id.swapRemoveAt(by_name_gop.index);
    const name = try strings.putWithoutDeduplication(new_username);
    std.debug.assert(username_to_user_id.swapRemove(account.name));
    account.name = name;
    by_name_gop.key_ptr.* = name;
    by_name_gop.value_ptr.* = user_id;
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
    return createAccount(&name_str, null, guest_perms);
}

pub fn ensureAdminUser(arena: Allocator) !void {
    if (haveAdminUser()) return;

    var name_str: ["Admin-123456".len]u8 = "Admin-XXXXXX".*;
    for (name_str[name_str.len - 6 ..]) |*c| {
        c.* = std.crypto.random.intRangeAtMost(u8, '0', '9');
    }

    var password_hash: PasswordHash = undefined;
    std.crypto.random.bytes(&password_hash.salt);
    var password_text: [16]u8 = undefined;
    for (&password_text) |*c| {
        c.* = std.crypto.random.intRangeAtMost(u8, ' ' + 1, '~');
    }
    {
        var h = std.crypto.hash.sha2.Sha256.init(.{});
        h.update(&password_hash.salt);
        h.update(&password_text);
        h.final(&password_hash.hash);
    }

    _ = try createAccount(&name_str, password_hash, .{
        .read = true,
        .add = true,
        .control = true,
        .playlist = true,
        .admin = true,
    });

    log.info("No admin account found. Created one:", .{});
    log.info("Username: {s}", .{name_str[0..]});
    log.info("Password: {s}", .{password_text[0..]});

    current_version = Id.random();
    try subscriptions.broadcastChanges(arena, .haveAdminUser);
    try subscriptions.broadcastChanges(arena, .users);
}

fn createAccount(name_str: []const u8, password_hash: ?PasswordHash, perms: Permissions) !Id {
    const by_name_gop = try username_to_user_id.getOrPutContextAdapted(g.gpa, name_str, StringsAdaptedContext{}, StringsContext{});
    if (by_name_gop.found_existing) @panic("unlikely"); // TODO: use generateIdAndPut() kinda thing.
    errdefer username_to_user_id.swapRemoveAt(by_name_gop.index);
    const name = try strings.putWithoutDeduplication(name_str);

    const user_id = Id.random();
    const gop = try user_accounts.getOrPut(user_id);
    if (gop.found_existing) @panic("unlikely"); // TODO: use generateIdAndPut() kinda thing.
    const user = gop.value_ptr;
    user.* = UserAccount{
        .name = name,
        .password_hash = password_hash,
        .registered = false,
        .requested = false,
        .approved = false,
        .perms = perms,
    };
    by_name_gop.key_ptr.* = name;
    by_name_gop.value_ptr.* = user_id;

    // publishing users event happens above this call.
    return user_id;
}

fn deleteAccount(user_id: Id) void {
    const account = user_accounts.fetchSwapRemove(user_id).?.value;
    std.debug.assert(username_to_user_id.swapRemove(account.name));
}

fn sendSelfUserInfo(client_id: *anyopaque) !void {
    const user_id = sessions.get(client_id).?.user_id;
    const account = user_accounts.get(user_id).?;
    try encodeAndSend(client_id, .{
        .user = .{
            .id = user_id,
            .name = strings.getString(account.name),
            .perms = account.perms,
            .registered = account.registered,
            .requested = account.requested,
            .approved = account.approved,
        },
    });
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
