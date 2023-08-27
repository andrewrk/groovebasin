const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;
const log = std.log;

const g = @import("global.zig");

const StringPool = @import("StringPool.zig");
const subscriptions = @import("subscriptions.zig");
const events = @import("events.zig");
const db = @import("db.zig");
const encodeAndSend = @import("server_main.zig").encodeAndSend;

const Id = @import("groovebasin_protocol.zig").Id;
const IdOrGuest = @import("groovebasin_protocol.zig").IdOrGuest;
const IdMap = @import("groovebasin_protocol.zig").IdMap;
const IdOrGuestMap = @import("groovebasin_protocol.zig").IdOrGuestMap;
const Permissions = @import("groovebasin_protocol.zig").Permissions;
const PublicUserInfo = @import("groovebasin_protocol.zig").PublicUserInfo;

const UserAccount = struct {
    /// Display name and login username.
    name: StringPool.Index,
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

pub var strings: StringPool = undefined;
pub var user_accounts: AutoArrayHashMap(Id, UserAccount) = undefined;
var username_to_user_id: std.ArrayHashMapUnmanaged(StringPool.Index, Id, StringsContext, true) = .{};
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
    pub fn hash(_: @This(), k: StringPool.Index) u32 {
        return @truncate(std.hash.Wyhash.hash(0, strings.getString(k)));
    }
    pub fn eql(_: @This(), a: StringPool.Index, b: StringPool.Index, _: usize) bool {
        return std.mem.eql(u8, strings.getString(a), strings.getString(b));
    }
};
const StringsAdaptedContext = struct {
    pub fn hash(_: @This(), k: []const u8) u32 {
        return @truncate(std.hash.Wyhash.hash(0, k));
    }
    pub fn eql(_: @This(), a: []const u8, b: StringPool.Index, _: usize) bool {
        return std.mem.eql(u8, a, strings.getString(b));
    }
};

pub fn init() !void {
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

pub fn handleLoaded() !void {
    try username_to_user_id.ensureTotalCapacity(g.gpa, user_accounts.count());
    var it = user_accounts.iterator();
    while (it.next()) |kv| {
        const id = kv.key_ptr.*;
        const account = kv.value_ptr;
        _ = username_to_user_id.putAssumeCapacity(account.name, id);
    }
}

pub fn handleClientConnected(changes: *db.Changes, client_id: *anyopaque) !void {
    // Every new connection starts as a guest.
    // If you want to be not a guest, send a login message.
    const user_id = try createGuestAccount(changes);
    try sessions.putNoClobber(client_id, .{
        .user_id = user_id,
        .claims_to_be_streaming = false,
    });

    try sendSelfUserInfo(client_id);
    changes.broadcastChanges(.users);
}

pub fn handleClientDisconnected(changes: *db.Changes, client_id: *anyopaque) !void {
    std.debug.assert(sessions.swapRemove(client_id));
    changes.broadcastChanges(.users);
}

pub fn getSessionPermissions(client_id: *anyopaque) Permissions {
    return user_accounts.get(userIdFromClientId(client_id)).?.perms;
}
pub fn userIdFromClientId(client_id: *anyopaque) Id {
    return sessions.get(client_id).?.user_id;
}

pub fn logout(changes: *db.Changes, client_id: *anyopaque) !void {
    const session = sessions.getEntry(client_id).?.value_ptr;
    session.user_id = try createGuestAccount(changes);
    try sendSelfUserInfo(client_id);
    changes.broadcastChanges(.users);
}

pub fn login(changes: *db.Changes, client_id: *anyopaque, username: []const u8, password: []u8) !void {
    defer std.crypto.utils.secureZero(u8, password);

    // Send this even in case of error.
    try changes.sendSelfUserInfo(client_id);

    loginImpl(changes, client_id, username, password) catch |err| {
        if (err == error.InvalidLogin) {
            // TODO: send the user an error?
        } else return err;
    };
}
fn loginImpl(changes: *db.Changes, client_id: *anyopaque, username: []const u8, password: []u8) !void {
    const session = sessions.getEntry(client_id).?.value_ptr;
    const session_account = user_accounts.getEntry(session.user_id).?.value_ptr;

    // We always need a password.
    const min_password_len = 1; // lmao
    if (password.len < min_password_len) return error.BadRequest; // password too short

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

    try changes.user_accounts.ensureUnusedCapacity(1);

    if (std.mem.eql(u8, username, strings.getString(session_account.name))) {
        // If you're trying to login to yourself, it always works.
        // Use this to change your password.
        changePassword(session_account, password);
        changes.user_accounts.putAssumeCapacity(session.user_id, {});
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
            try mergeAccounts(changes, guest_user_id, target_user_id);
        }
    } else {
        // Create account: change username, change password, and register.
        try changeUserName(changes, session.user_id, username);
        changePassword(session_account, password);
        session_account.registered = true;
        changes.user_accounts.putAssumeCapacity(session.user_id, {});
    }

    changes.broadcastChanges(.users);
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

fn changeUserName(changes: *db.Changes, user_id: Id, new_username: []const u8) !void {
    try username_to_user_id.ensureUnusedCapacity(g.gpa, 1);
    try strings.ensureUnusedCapacity(new_username.len);
    try changes.user_accounts.ensureUnusedCapacity(1);
    const account = user_accounts.getEntry(user_id).?.value_ptr;

    std.debug.assert(username_to_user_id.swapRemove(account.name));
    const name = strings.putWithoutDeduplicationAssumeCapacity(new_username);
    const by_name_gop = username_to_user_id.getOrPutAssumeCapacityAdapted(new_username, StringsAdaptedContext{});
    std.debug.assert(!by_name_gop.found_existing);
    account.name = name;
    by_name_gop.key_ptr.* = name;
    by_name_gop.value_ptr.* = user_id;

    changes.user_accounts.putAssumeCapacity(user_id, {});
}

pub fn setStreaming(changes: *db.Changes, client_id: *anyopaque, is_streaming: bool) !void {
    sessions.getEntry(client_id).?.value_ptr.claims_to_be_streaming = is_streaming;

    changes.broadcastChanges(.users);
}

pub fn haveAdminUser() bool {
    for (user_accounts.values()) |*user| {
        if (user.perms.admin) return true;
    }
    return false;
}

fn createGuestAccount(changes: *db.Changes) !Id {
    var name_str: ["Guest-123456".len]u8 = "Guest-XXXXXX".*;
    for (name_str[name_str.len - 6 ..]) |*c| {
        c.* = std.base64.url_safe_alphabet_chars[std.crypto.random.int(u6)];
    }
    return createAccount(changes, &name_str, null, false, guest_perms);
}

pub fn ensureAdminUser(changes: *db.Changes) !void {
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

    _ = try createAccount(changes, &name_str, password_hash, true, .{
        .read = true,
        .add = true,
        .control = true,
        .playlist = true,
        .admin = true,
    });

    log.info("No admin account found. Created one:", .{});
    log.info("Username: {s}", .{name_str[0..]});
    log.info("Password: {s}", .{password_text[0..]});

    changes.broadcastChanges(.haveAdminUser);
}

pub fn requestApproval(changes: *db.Changes, client_id: *anyopaque) !void {
    const user_id = sessions.get(client_id).?.user_id;
    const account = user_accounts.getEntry(user_id).?.value_ptr;
    if (account.requested) return; // You're good.

    try changes.user_accounts.put(user_id, {});
    account.requested = true;
    changes.broadcastChanges(.users);
}

pub fn approve(changes: *db.Changes, args: anytype) error{OutOfMemory}!void {
    // the most changes from a single approval is:
    // * deleting (merging) one user
    // * name changing the other user
    try changes.user_accounts.ensureUnusedCapacity(2 * args.len);

    for (args) |approval| {
        var requesting_user_id = approval.id;
        const replace_user_id = approval.replaceId orelse requesting_user_id;
        const is_approved = approval.approved;
        const new_name = approval.name;

        var requesting_account = (user_accounts.getEntry(requesting_user_id) orelse {
            log.warn("ignoring bogus userid: {}", .{requesting_user_id});
            continue;
        }).value_ptr;
        if (!user_accounts.contains(replace_user_id)) {
            log.warn("ignoring bogus replace userid: {}", .{replace_user_id});
            continue;
        }
        try sendSelfUserInfoForUserId(changes, requesting_user_id);
        if (!is_approved) {
            // Just undo the request.
            requesting_account.requested = false;
            changes.user_accounts.putAssumeCapacity(requesting_user_id, {});
            continue;
        }

        if (replace_user_id.value != requesting_user_id.value) {
            try mergeAccounts(changes, requesting_user_id, replace_user_id);
            requesting_user_id = replace_user_id;
            requesting_account = user_accounts.getEntry(requesting_user_id).?.value_ptr;
        } else {
            requesting_account.approved = true;
            changes.user_accounts.putAssumeCapacity(requesting_user_id, {});
        }

        const old_name = strings.getString(requesting_account.name);
        if (!std.mem.eql(u8, old_name, new_name)) {
            // This is also a feature of the approve workflow.
            try changeUserName(changes, requesting_user_id, new_name);
        }
    }

    changes.broadcastChanges(.users);
}

pub fn updateUser(changes: *db.Changes, user_id_or_guest_pseudo_user_id: IdOrGuest, perms: Permissions) !void {
    const old_have_admin_user = haveAdminUser();

    switch (user_id_or_guest_pseudo_user_id) {
        .id => |user_id| {
            const account = (user_accounts.getEntry(user_id) orelse {
                log.warn("ignoring bogus userid: {}", .{user_id});
                return;
            }).value_ptr;
            try sendSelfUserInfoForUserId(changes, user_id);
            try changes.user_accounts.put(user_id, {});
            account.perms = perms;
        },
        .guest => {
            guest_perms = perms;
            //changes.guest_perms = true;
            var it = user_accounts.iterator();
            while (it.next()) |kv| {
                const user_id = kv.key_ptr.*;
                const account = kv.value_ptr;
                if (account.approved) continue;

                try sendSelfUserInfoForUserId(changes, user_id);
                try changes.user_accounts.put(user_id, {});
                account.perms = guest_perms;
            }
        },
    }

    changes.broadcastChanges(.users);
    if (old_have_admin_user != haveAdminUser()) {
        changes.broadcastChanges(.haveAdminUser);
    }
}

pub fn deleteUsers(changes: *db.Changes, user_ids: []const Id) !void {
    try changes.user_accounts.ensureUnusedCapacity(user_ids.len);
    const old_have_admin_user = haveAdminUser();

    for (user_ids) |user_id| {
        var it = sessions.iterator();
        while (it.next()) |kv| {
            if (kv.value_ptr.user_id.value == user_id.value) {
                kv.value_ptr.user_id = try createGuestAccount(changes);
                try changes.sendSelfUserInfo(kv.key_ptr.*);
            }
        }
        try events.tombstoneUser(changes, user_id);
        changes.user_accounts.putAssumeCapacity(user_id, {});
        deleteAccount(user_id);
    }

    changes.broadcastChanges(.users);
    if (old_have_admin_user != haveAdminUser()) {
        changes.broadcastChanges(.haveAdminUser);
    }
}

fn createAccount(
    changes: *db.Changes,
    name_str: []const u8,
    password_hash: ?PasswordHash,
    registered_requested_approved: bool,
    perms: Permissions,
) !Id {
    try user_accounts.ensureUnusedCapacity(1);
    try changes.user_accounts.ensureUnusedCapacity(1);
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
    changes.user_accounts.putAssumeCapacity(user_id, {});
    changes.broadcastChanges(.users);

    // publishing users event happens above this call.
    return user_id;
}

fn mergeAccounts(changes: *db.Changes, doomed_user_id: Id, true_user_id: Id) !void {
    try changes.user_accounts.ensureUnusedCapacity(1);
    const workaround_miscomiplation = doomed_user_id.value; // FIXME: Test that this is fixed by logging in to an existing account.
    // We're about to delete this user, so make sure all sessions get upgraded.
    var it = sessions.iterator();
    while (it.next()) |kv| {
        if (kv.value_ptr.user_id.value == doomed_user_id.value) {
            kv.value_ptr.user_id = true_user_id;
            try changes.sendSelfUserInfo(kv.key_ptr.*);
        }
    }
    try events.revealTrueIdentity(changes, doomed_user_id, true_user_id);
    changes.user_accounts.putAssumeCapacity(Id{ .value = workaround_miscomiplation }, {});
    deleteAccount(Id{ .value = workaround_miscomiplation });
}

fn sendSelfUserInfoForUserId(changes: *db.Changes, user_id: Id) !void {
    var it = sessions.iterator();
    while (it.next()) |kv| {
        if (kv.value_ptr.user_id.value == user_id.value) {
            try changes.sendSelfUserInfo(kv.key_ptr.*);
        }
    }
}

fn deleteAccount(user_id: Id) void {
    const account = user_accounts.fetchSwapRemove(user_id).?.value;
    std.debug.assert(username_to_user_id.swapRemove(account.name));
}

pub fn sendSelfUserInfo(client_id: *anyopaque) !void {
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

pub fn getSerializable(arena: Allocator, out_version: *?Id) !IdOrGuestMap(PublicUserInfo) {
    // This version number never matters. Sessions beginning/ending changes the
    // hash, which means there's no scenario in which a newly connecting client
    // would get a cache hit on their previous version of the users. Delta
    // compression for live connections is still meaningful, so we don't want
    // this to stay null, but this number never does anything meaningful.
    out_version.* = Id.random();

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
