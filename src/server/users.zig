const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;
const log = std.log;

const g = @import("global.zig");

const StringPool = @import("shared").StringPool;
const subscriptions = @import("subscriptions.zig");
const events = @import("events.zig");
const encodeAndSend = @import("server_main.zig").encodeAndSend;

const Id = @import("groovebasin_protocol.zig").Id;
const IdOrGuest = @import("groovebasin_protocol.zig").IdOrGuest;
const IdMap = @import("groovebasin_protocol.zig").IdMap;
const IdOrGuestMap = @import("groovebasin_protocol.zig").IdOrGuestMap;
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
    .add = false,
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

pub fn getSessionPermissions(client_id: *anyopaque) Permissions {
    return user_accounts.get(sessions.get(client_id).?.user_id).?.perms;
}

pub fn logout(arena: Allocator, client_id: *anyopaque) !void {
    const session = sessions.getEntry(client_id).?.value_ptr;
    session.user_id = try createGuestAccount();
    try sendSelfUserInfo(client_id);
    try subscriptions.broadcastChanges(arena, .users);
}

pub fn login(arena: Allocator, client_id: *anyopaque, username: []const u8, password: []u8) !void {
    defer shred(password);
    var sessions_to_notify = std.ArrayList(*anyopaque).init(arena);
    try sessions_to_notify.append(client_id);
    loginImpl(arena, client_id, username, password, &sessions_to_notify) catch |err| {
        if (err == error.InvalidLogin) {
            // TODO: send the user an error?
        } else return err;
    };
    try sendSelfUserInfoDeduplicated(sessions_to_notify.items);
}
fn loginImpl(
    arena: Allocator,
    client_id: *anyopaque,
    username: []const u8,
    password: []u8,
    sessions_to_notify: *std.ArrayList(*anyopaque),
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
            try mergeAccounts(guest_user_id, target_user_id, sessions_to_notify);
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
    try username_to_user_id.ensureUnusedCapacity(g.gpa, 1);
    try strings.ensureUnusedCapacity(new_username.len);
    const account = user_accounts.getEntry(user_id).?.value_ptr;

    std.debug.assert(username_to_user_id.swapRemove(account.name));
    const name = strings.putWithoutDeduplicationAssumeCapacity(new_username);
    const by_name_gop = username_to_user_id.getOrPutAssumeCapacityAdapted(new_username, StringsAdaptedContext{});
    std.debug.assert(!by_name_gop.found_existing);
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
    return createAccount(&name_str, null, false, guest_perms);
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

    _ = try createAccount(&name_str, password_hash, true, .{
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

pub fn requestApproval(arena: Allocator, client_id: *anyopaque) !void {
    const account = user_accounts.getEntry(sessions.get(client_id).?.user_id).?.value_ptr;

    account.requested = true;

    try subscriptions.broadcastChanges(arena, .users);
}

pub fn approve(arena: Allocator, args: anytype) error{OutOfMemory}!void {
    // This might end up with duplicates:
    var sessions_to_notify = std.ArrayList(*anyopaque).init(arena);
    for (args) |approval| {
        var requesting_user_id = approval.id;
        const replace_user_id = approval.replaceId;
        const is_approved = approval.approved;
        const new_name = approval.name;

        var requesting_account = (user_accounts.getEntry(requesting_user_id) orelse {
            log.warn("ignoring bogus userid: {}", .{requesting_user_id});
            continue;
        }).value_ptr;
        try collectSessionsForAccount(requesting_user_id, &sessions_to_notify);
        if (!is_approved) {
            // Just undo the request.
            requesting_account.requested = false;
            continue;
        }

        if (replace_user_id) |true_user_id| {
            try mergeAccounts(requesting_user_id, true_user_id, &sessions_to_notify);
            requesting_user_id = true_user_id;
            requesting_account = user_accounts.getEntry(requesting_user_id).?.value_ptr;
        } else {
            requesting_account.approved = true;
        }

        const old_name = strings.getString(requesting_account.name);
        if (!std.mem.eql(u8, old_name, new_name)) {
            // This is also a feature of the approve workflow.
            try changeUserName(requesting_user_id, new_name);
        }
    }

    try sendSelfUserInfoDeduplicated(sessions_to_notify.items);
    try subscriptions.broadcastChanges(arena, .users);
}

pub fn updateUser(arena: Allocator, user_id_or_guest_pseudo_user_id: IdOrGuest, perms: Permissions) !void {
    const old_have_admin_user = haveAdminUser();
    var sessions_to_notify = std.ArrayList(*anyopaque).init(arena);

    switch (user_id_or_guest_pseudo_user_id) {
        .id => |user_id| {
            const account = user_accounts.getEntry(user_id).?.value_ptr;
            account.perms = perms;
            try collectSessionsForAccount(user_id, &sessions_to_notify);
        },
        .guest => {
            guest_perms = perms;
            var it = user_accounts.iterator();
            while (it.next()) |kv| {
                const user_id = kv.key_ptr.*;
                const account = kv.value_ptr;
                if (account.approved) continue;
                account.perms = guest_perms;
                try collectSessionsForAccount(user_id, &sessions_to_notify);
            }
        },
    }

    try sendSelfUserInfoDeduplicated(sessions_to_notify.items);
    try subscriptions.broadcastChanges(arena, .users);
    if (old_have_admin_user != haveAdminUser()) {
        try subscriptions.broadcastChanges(arena, .haveAdminUser);
    }
}

pub fn deleteUsers(arena: Allocator, user_ids: []const Id) !void {
    const old_have_admin_user = haveAdminUser();
    var sessions_to_notify = std.ArrayList(*anyopaque).init(arena);

    for (user_ids) |user_id| {
        var it = sessions.iterator();
        while (it.next()) |kv| {
            if (kv.value_ptr.user_id.value == user_id.value) {
                kv.value_ptr.user_id = try createGuestAccount();
                try sessions_to_notify.append(kv.key_ptr.*);
            }
        }
        try events.tombstoneUser(user_id);
        deleteAccount(user_id);
    }

    try sendSelfUserInfoDeduplicated(sessions_to_notify.items);
    try subscriptions.broadcastChanges(arena, .users);
    if (old_have_admin_user != haveAdminUser()) {
        try subscriptions.broadcastChanges(arena, .haveAdminUser);
    }
}

fn createAccount(
    name_str: []const u8,
    password_hash: ?PasswordHash,
    registered_requested_approved: bool,
    perms: Permissions,
) !Id {
    try user_accounts.ensureUnusedCapacity(1);
    try username_to_user_id.ensureUnusedCapacity(g.gpa, 1);
    try strings.ensureUnusedCapacity(name_str.len);

    const by_name_gop = username_to_user_id.getOrPutAssumeCapacityAdapted(name_str, StringsAdaptedContext{});
    if (by_name_gop.found_existing) @panic("unlikely"); // TODO: use generateIdAndPut() kinda thing.
    const name = strings.putWithoutDeduplicationAssumeCapacity(name_str);

    const user_id = Id.random();
    const gop = user_accounts.getOrPutAssumeCapacity(user_id);
    if (gop.found_existing) @panic("unlikely"); // TODO: use generateIdAndPut() kinda thing.
    const user = gop.value_ptr;
    user.* = UserAccount{
        .name = name,
        .password_hash = password_hash,
        .registered = registered_requested_approved,
        .requested = registered_requested_approved,
        .approved = registered_requested_approved,
        .perms = perms,
    };
    by_name_gop.key_ptr.* = name;
    by_name_gop.value_ptr.* = user_id;

    // publishing users event happens above this call.
    return user_id;
}

fn mergeAccounts(doomed_user_id: Id, true_user_id: Id, sessions_to_notify: *ArrayList(*anyopaque)) !void {
    const workaround_miscomiplation = doomed_user_id.value; // FIXME: Test that this is fixed by logging in to an existing account.
    // We're about to delete this user, so make sure all sessions get upgraded.
    var it = sessions.iterator();
    while (it.next()) |kv| {
        if (kv.value_ptr.user_id.value == doomed_user_id.value) {
            kv.value_ptr.user_id = true_user_id;
            try sessions_to_notify.append(kv.key_ptr.*);
        }
    }
    try events.revealTrueIdentity(doomed_user_id, true_user_id);
    deleteAccount(Id{ .value = workaround_miscomiplation });
}

fn collectSessionsForAccount(user_id: Id, out_sessions: *ArrayList(*anyopaque)) !void {
    var it = sessions.iterator();
    while (it.next()) |kv| {
        if (kv.value_ptr.user_id.value == user_id.value) {
            try out_sessions.append(kv.key_ptr.*);
        }
    }
}
fn sendSelfUserInfoDeduplicated(sessions_to_notify: []const *anyopaque) !void {
    for (sessions_to_notify, 0..) |client_id, i| {
        // dedup
        for (0..i) |j| {
            if (sessions_to_notify[i] == sessions_to_notify[j]) break;
        } else {
            try sendSelfUserInfo(client_id);
        }
    }
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

pub fn getSerializable(arena: Allocator) !IdOrGuestMap(PublicUserInfo) {
    var result = IdOrGuestMap(PublicUserInfo){};
    try result.map.ensureTotalCapacity(arena, user_accounts.count() + 1);

    var it = user_accounts.iterator();
    while (it.next()) |kv| {
        const user_id = kv.key_ptr.*;
        const account = kv.value_ptr;
        result.map.putAssumeCapacityNoClobber(.{ .id = user_id }, .{
            .name = strings.getString(account.name),
            .perms = account.perms,
            .requested = account.requested,
            .approved = account.approved,
            .connected = false,
            .streaming = false,
        });
    }

    for (sessions.values()) |*session| {
        var public_info = result.map.getEntry(.{ .id = session.user_id }).?.value_ptr;
        public_info.connected = true;
        if (session.claims_to_be_streaming) {
            public_info.streaming = true;
        }
    }

    result.map.putAssumeCapacityNoClobber(.guest, .{
        // Displayed in the permission edit UI:
        .name = "Guests",
        // This is the important information:
        .perms = guest_perms,
        // This gest the pseudo user to show up in the permission edit UI:
        .requested = true,
        .approved = true,
        // Unused:
        .connected = false,
        .streaming = false,
    });

    return result;
}
