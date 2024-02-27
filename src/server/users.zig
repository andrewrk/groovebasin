const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;
const log = std.log;

const StringPool = @import("StringPool.zig");
const subscriptions = @import("subscriptions.zig");
const events = @import("events.zig");
const db = @import("db.zig");
const encodeAndSend = @import("server_main.zig").encodeAndSend;

const protocol = @import("groovebasin_protocol.zig");
const Id = protocol.Id;
const IdMap = protocol.IdMap;
const Permissions = protocol.Permissions;

const InternalSession = db.InternalSession;
const UserAccount = db.UserAccount;
const RegistrationStage = db.RegistrationStage;
const InternalPermissions = db.InternalPermissions;
const PasswordHash = db.PasswordHash;

pub fn handleClientConnected(client_id: Id) !void {
    const g = @import("global.zig");
    const sessions = &g.the_database.sessions;
    // Every new connection starts as a guest.
    // If you want to be not a guest, send a login message.
    const user_id = try createGuestAccount();
    try sessions.putNoClobber(client_id, .{
        .user_id = user_id,
        .claims_to_be_streaming = false,
    });

    try encodeAndSend(client_id, .{
        .sessionId = client_id,
    });
}

pub fn handleClientDisconnected(client_id: Id) !void {
    const g = @import("global.zig");
    const sessions = &g.the_database.sessions;
    try sessions.remove(client_id);
}

pub fn getUserId(client_id: Id) Id {
    const g = @import("global.zig");
    const sessions = &g.the_database.sessions;
    return sessions.get(client_id).user_id;
}
pub fn getPermissions(user_id: Id) InternalPermissions {
    const g = @import("global.zig");
    const user_accounts = &g.the_database.user_accounts;

    return user_accounts.get(user_id).permissions;
}

pub fn logout(client_id: Id) !void {
    const g = @import("global.zig");
    const sessions = &g.the_database.sessions;
    const session = try sessions.getForEditing(client_id);
    session.user_id = try createGuestAccount();
}

pub fn login(client_id: Id, username: []const u8, password: []u8) !void {
    defer std.crypto.utils.secureZero(u8, password);

    loginImpl(client_id, username, password) catch |err| {
        if (err == error.InvalidLogin) {
            // TODO: send the user an error?
            log.debug("invalid login for client id: {}", .{client_id});
        } else return err;
    };
}
fn loginImpl(client_id: Id, username_str: []const u8, password: []u8) !void {
    const g = @import("global.zig");
    const sessions = &g.the_database.sessions;
    const user_accounts = &g.the_database.user_accounts;

    // We always need a password.
    const min_password_len = 1; // lmao
    if (password.len < min_password_len) return error.BadRequest; // password too short
    const username = try g.strings.put(g.gpa, username_str);

    const session = try sessions.getForEditing(client_id);
    const session_account = try user_accounts.getForEditing(session.user_id);

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

    if (session_account.username == username) {
        // If you're trying to login to yourself, it always works.
        // Use this to change your password.
        changePassword(session_account, password);
        return;
    }

    if (lookupAccountByUsername(username)) |target_user_id| {
        // Login check.
        const target_account = user_accounts.get(target_user_id);
        try checkPassword(target_account, password);
        // You're in.
        switch (session_account.registration_stage) {
            .guest_without_password, .guest_with_password => {
                // An actual regular login, which is the most complex case.
                const guest_user_id = session.user_id;
                try mergeAccounts(guest_user_id, target_user_id);
            },
            .named_by_user, .requested_approval, .approved => {
                // Switch login.
                session.user_id = target_user_id;
            },
        }
    } else {
        // change username, change password. Sorta like "creating an account".
        session_account.username = username;
        changePassword(session_account, password);
        session_account.registration_stage = .named_by_user;
    }
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
    if (account.registration_stage == .guest_without_password) return error.InvalidLogin;

    var h = std.crypto.hash.sha2.Sha256.init(.{});
    h.update(&account.password_hash.salt);
    h.update(password);
    var hash: [32]u8 = undefined;
    h.final(&hash);

    if (!std.mem.eql(u8, &hash, &account.password_hash.hash)) return error.InvalidLogin;
}

pub fn setStreaming(client_id: Id, is_streaming: bool) !void {
    const g = @import("global.zig");
    const sessions = &g.the_database.sessions;
    const account = try sessions.getForEditing(client_id);
    account.claims_to_be_streaming = is_streaming;
}

pub fn broadcastSeekEvent() !void {
    const g = @import("global.zig");
    const sessions = &g.the_database.sessions;
    for (sessions.table.keys(), sessions.table.values()) |client_id, account| {
        if (!account.claims_to_be_streaming) continue;
        try encodeAndSend(client_id, .seek);
    }
}

fn lookupAccountByUsername(username: StringPool.Index) ?Id {
    const g = @import("global.zig");
    const user_accounts = &g.the_database.user_accounts;

    var it = user_accounts.iterator();
    while (it.next()) |kv| {
        const user_id = kv.key_ptr.*;
        const account = kv.value_ptr;
        if (account.username == username) return user_id;
    }
    return null;
}

fn createGuestAccount() !Id {
    const g = @import("global.zig");

    var username_str: ["Guest-123456".len]u8 = "Guest-XXXXXX".*;
    for (username_str[username_str.len - 6 ..]) |*c| {
        c.* = std.base64.url_safe_alphabet_chars[std.crypto.random.int(u6)];
    }
    return createAccount(
        &username_str,
        std.mem.zeroes(PasswordHash),
        .guest_without_password,
        g.the_database.getState().guest_permissions,
    );
}

pub fn ensureAdminUser() !void {
    const g = @import("global.zig");
    const user_accounts = &g.the_database.user_accounts;

    {
        var it = user_accounts.iterator();
        while (it.next()) |kv| {
            const account = kv.value_ptr;
            if (account.permissions.admin) {
                log.warn("ignoring ensureAdminUser. there's already an admin user.", .{});
                return;
            }
        }
    }

    var username_str: ["Admin-123456".len]u8 = "Admin-XXXXXX".*;
    for (username_str[username_str.len - 6 ..]) |*c| {
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

    _ = try createAccount(&username_str, password_hash, .approved, .{
        .read = true,
        .add = true,
        .control = true,
        .playlist = true,
        .admin = true,
    });

    log.info("No admin account found. Created one:", .{});
    log.info("Username: {s}", .{username_str[0..]});
    log.info("Password: {s}", .{password_text[0..]});
}

pub fn requestApproval(client_id: Id) !void {
    const g = @import("global.zig");
    const user_accounts = &g.the_database.user_accounts;

    const user_id = getUserId(client_id);
    const account = try user_accounts.getForEditing(user_id);
    switch (account.registration_stage) {
        .named_by_user => {
            account.registration_stage = .requested_approval;
        },
        else => {
            log.warn("ignoring request for approval from account in wrong registration stage. {}: {}", .{
                user_id, account.registration_stage,
            });
        },
    }
}

pub fn approve(args: anytype) error{OutOfMemory}!void {
    const g = @import("global.zig");
    const user_accounts = &g.the_database.user_accounts;

    for (args) |approval| {
        var requesting_user_id = approval.id;
        const replace_user_id = approval.replaceId orelse requesting_user_id;
        const is_approved = approval.approved;
        const new_username_str = approval.name;

        if (!user_accounts.contains(requesting_user_id)) {
            log.warn("ignoring bogus requesting user id: {}", .{replace_user_id});
            continue;
        }
        if (replace_user_id.value != requesting_user_id.value and !user_accounts.contains(replace_user_id)) {
            log.warn("ignoring bogus replace user id: {}", .{replace_user_id});
            continue;
        }
        var requesting_account = try user_accounts.getForEditing(requesting_user_id);
        if (requesting_account.registration_stage != .requested_approval) {
            log.warn("ignoring approve decision for not-requested user id: {}", .{requesting_user_id});
            continue;
        }
        if (!is_approved) {
            // Just undo the request.
            requesting_account.registration_stage = .named_by_user;
            continue;
        }

        if (replace_user_id.value != requesting_user_id.value) {
            try mergeAccounts(requesting_user_id, replace_user_id);
            requesting_user_id = replace_user_id;
            requesting_account = try user_accounts.getForEditing(requesting_user_id);
        } else {
            requesting_account.registration_stage = .approved;
        }

        const old_len = g.strings.len();
        const new_username = try g.strings.put(g.gpa, new_username_str);
        const is_newly_added = g.strings.len() > old_len;
        if (requesting_account.username != new_username) {
            // This is also a feature of the approve workflow.
            // The admin edited the name.
            if (is_newly_added) {
                requesting_account.username = new_username;
            } else {
                log.warn("ignoring attempt by admin to rename user to cause a username collision: {s}", .{new_username_str});
            }
        }
    }
}

pub fn updateUser(user_id: Id, perms: Permissions) !void {
    const g = @import("global.zig");
    const user_accounts = &g.the_database.user_accounts;

    if (!user_accounts.contains(user_id)) {
        log.warn("ignoring bogus userid: {}", .{user_id});
        return;
    }
    const account = try user_accounts.getForEditing(user_id);
    account.permissions = convertPermsissions(perms);
}

pub fn updateGuestPermissions(perms: Permissions) !void {
    const g = @import("global.zig");
    const user_accounts = &g.the_database.user_accounts;

    const permissions = convertPermsissions(perms);
    g.the_database.getStateForEditing().guest_permissions = permissions;
    var it = user_accounts.iterator();
    while (it.next()) |kv| {
        const account = kv.value_ptr;
        if (account.registration_stage == .approved) continue;
        const account_for_editing = try it.promoteForEditing(kv);
        account_for_editing.permissions = permissions;
    }
}

pub fn deleteUsers(user_ids: []const Id) !void {
    const g = @import("global.zig");
    const sessions = &g.the_database.sessions;
    const user_accounts = &g.the_database.user_accounts;

    var replacement_guest_id: ?Id = null;
    for (user_ids) |user_id| {
        if (!user_accounts.contains(user_id)) {
            log.warn("ignoring bogus delete user id: {}", .{user_id});
            continue;
        }
        var it = sessions.iterator();
        while (it.next()) |kv| {
            if (kv.value_ptr.user_id.value == user_id.value) {
                if (replacement_guest_id == null) {
                    replacement_guest_id = try createGuestAccount();
                }
                const account_for_editing = try it.promoteForEditing(kv);
                account_for_editing.user_id = replacement_guest_id.?;
            }
        }
        try events.tombstoneUser(user_id);
        try user_accounts.remove(user_id);
    }
}

fn createAccount(
    username_str: []const u8,
    password_hash: PasswordHash,
    registration_stage: RegistrationStage,
    permissions: InternalPermissions,
) !Id {
    const g = @import("global.zig");
    const user_accounts = &g.the_database.user_accounts;

    const username = try g.strings.put(g.gpa, username_str);

    const user_id = try user_accounts.putRandom(.{
        .username = username,
        .password_hash = password_hash,
        .registration_stage = registration_stage,
        .permissions = permissions,
    });

    return user_id;
}

fn mergeAccounts(doomed_user_id: Id, true_user_id: Id) !void {
    const g = @import("global.zig");
    const sessions = &g.the_database.sessions;
    const user_accounts = &g.the_database.user_accounts;

    const workaround_miscomiplation = doomed_user_id.value; // FIXME: Test that this is fixed by logging in to an existing account.
    // We're about to delete this user, so make sure all sessions get upgraded.
    var it = sessions.iterator();
    while (it.next()) |kv| {
        if (kv.value_ptr.user_id.value == doomed_user_id.value) {
            const account_for_editing = try it.promoteForEditing(kv);
            account_for_editing.user_id = true_user_id;
        }
    }
    try events.revealTrueIdentity(doomed_user_id, true_user_id);
    try user_accounts.remove(Id{ .value = workaround_miscomiplation });
}

pub fn serializableSession(session: InternalSession) protocol.Session {
    return .{
        .userId = session.user_id,
        .streaming = session.claims_to_be_streaming,
    };
}

pub fn serializableUserAccount(account: UserAccount) protocol.PublicUserInfo {
    const g = @import("global.zig");

    return .{
        .name = g.strings.get(account.username),
        .perms = convertPermsissions(account.permissions),
        .registration = switch (account.registration_stage) {
            .guest_without_password, .guest_with_password => .guest,
            .named_by_user => .named_by_user,
            .requested_approval => .requested_approval,
            .approved => .approved,
        },
    };
}

pub fn convertPermsissions(other_permissions: anytype) if (@TypeOf(other_permissions) == Permissions) InternalPermissions else Permissions {
    return .{
        .read = other_permissions.read,
        .add = other_permissions.add,
        .control = other_permissions.control,
        .playlist = other_permissions.playlist,
        .admin = other_permissions.admin,
    };
}
