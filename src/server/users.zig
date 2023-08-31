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

const protocol = @import("groovebasin_protocol.zig");
const Id = protocol.Id;
const IdOrGuest = protocol.IdOrGuest;
const IdOrGuestMap = protocol.IdOrGuestMap;
const Permissions = protocol.Permissions;
const PublicUserInfo = protocol.PublicUserInfo;

const UserAccount = struct {
    /// Display name and login username.
    username: StringPool.Index,
    password_hash: PasswordHash,
    registration_stage: RegistrationStage,
    permissions: InternalPermissions,
};

const RegistrationStage = enum(u3) {
    /// Freshly generated.
    guest_without_password = 0,
    /// Clients can (and do) set a random password automatically for guest accounts.
    guest_with_password = 1,
    /// If the user ever changes their name, they get here. This is required to advance.
    named_by_user = 2,
    /// Makes the user show up in the admin approval view.
    requested_approval = 3,
    /// Finally the user can have non-guest permissions.
    approved = 4,
    _,
};
const InternalPermissions = packed struct {
    read: bool,
    add: bool,
    control: bool,
    playlist: bool,
    admin: bool,
};

/// Uses sha256
const PasswordHash = struct {
    salt: [16]u8,
    hash: [32]u8,
};

const InternalSession = struct {
    user_id: Id,
    claims_to_be_streaming: bool = false,
};

// TODO: These are only pub for the db subsystem to access. consider a more constrained inter-module api.
pub var strings: StringPool = .{};
pub var user_accounts: AutoArrayHashMap(Id, UserAccount) = undefined;
var sessions: db.Database(Id, InternalSession, .sessions) = undefined;
var guest_perms: InternalPermissions = .{
    // Can be changed by admins.
    .read = true,
    .add = false,
    .control = true,
    .playlist = false,
    .admin = false,
};

pub fn init() !void {
    user_accounts = AutoArrayHashMap(Id, UserAccount).init(g.gpa);
    sessions = db.Database(Id, InternalSession, .sessions).init(g.gpa);
}
pub fn deinit() void {
    strings.deinit(g.gpa);
    user_accounts.deinit();
    sessions.deinit();
}

pub fn handleClientConnected(changes: *db.Changes, client_id: Id) !void {
    // Every new connection starts as a guest.
    // If you want to be not a guest, send a login message.
    const user_id = try createGuestAccount(changes);
    try sessions.putNoClobber(changes, client_id, .{
        .user_id = user_id,
        .claims_to_be_streaming = false,
    });

    try encodeAndSend(client_id, .{
        .sessionId = client_id,
    });
}

pub fn handleClientDisconnected(changes: *db.Changes, client_id: Id) !void {
    sessions.remove(changes, client_id);
}

pub fn getUserId(client_id: Id) Id {
    return sessions.get(client_id).user_id;
}
pub fn getPermissions(user_id: Id) InternalPermissions {
    return getUserAccount(user_id).permissions;
}

pub fn logout(changes: *db.Changes, client_id: Id) !void {
    const session = try sessions.getForEditing(changes, client_id);
    session.user_id = try createGuestAccount(changes);
}

pub fn login(changes: *db.Changes, client_id: Id, username: []const u8, password: []u8) !void {
    defer std.crypto.utils.secureZero(u8, password);

    loginImpl(changes, client_id, username, password) catch |err| {
        if (err == error.InvalidLogin) {
            // TODO: send the user an error?
            log.debug("invalid login for client id: {}", .{client_id});
        } else return err;
    };
}
fn loginImpl(changes: *db.Changes, client_id: Id, username_str: []const u8, password: []u8) !void {
    // We always need a password.
    const min_password_len = 1; // lmao
    if (password.len < min_password_len) return error.BadRequest; // password too short
    const username = try strings.put(g.gpa, username_str);

    const session = try sessions.getForEditing(changes, client_id);
    const session_account = try getUserAccountForEditing(changes, session.user_id);

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
        const target_account = getUserAccount(target_user_id);
        try checkPassword(target_account, password);
        // You're in.
        if (@intFromEnum(session_account.registration_stage) >= @intFromEnum(RegistrationStage.named_by_user)) {
            // Switch login.
            session.user_id = target_user_id;
        } else {
            // An actual regular login, which is the most complex case.
            const guest_user_id = session.user_id;
            try mergeAccounts(changes, guest_user_id, target_user_id);
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

pub fn setStreaming(changes: *db.Changes, client_id: Id, is_streaming: bool) !void {
    const account = try sessions.getForEditing(changes, client_id);
    account.claims_to_be_streaming = is_streaming;
}

fn lookupAccountByUsername(username: StringPool.Index) ?Id {
    var it = user_accounts.iterator();
    while (it.next()) |kv| {
        const user_id = kv.key_ptr.*;
        const account = kv.value_ptr;
        if (account.username == username) return user_id;
    }
    return null;
}

fn createGuestAccount(changes: *db.Changes) !Id {
    var username_str: ["Guest-123456".len]u8 = "Guest-XXXXXX".*;
    for (username_str[username_str.len - 6 ..]) |*c| {
        c.* = std.base64.url_safe_alphabet_chars[std.crypto.random.int(u6)];
    }
    return createAccount(changes, &username_str, std.mem.zeroes(PasswordHash), .guest_without_password, guest_perms);
}

pub fn ensureAdminUser(changes: *db.Changes) !void {
    for (user_accounts.values()) |*user| {
        if (user.permissions.admin) {
            log.warn("ignoring ensureAdminUser. there's already an admin user.", .{});
            return;
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

    _ = try createAccount(changes, &username_str, password_hash, .approved, .{
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

pub fn requestApproval(changes: *db.Changes, client_id: Id) !void {
    const user_id = getUserId(client_id);
    const account = try getUserAccountForEditing(changes, user_id);
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

pub fn approve(changes: *db.Changes, args: anytype) error{OutOfMemory}!void {
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
        var requesting_account = try getUserAccountForEditing(changes, requesting_user_id);
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
            try mergeAccounts(changes, requesting_user_id, replace_user_id);
            requesting_user_id = replace_user_id;
            requesting_account = try getUserAccountForEditing(changes, requesting_user_id);
        } else {
            requesting_account.registration_stage = .approved;
        }

        const old_len = strings.len();
        const new_username = try strings.put(g.gpa, new_username_str);
        const is_newly_added = strings.len() > old_len;
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

pub fn updateUser(changes: *db.Changes, user_id_or_guest_pseudo_user_id: IdOrGuest, perms: Permissions) !void {
    switch (user_id_or_guest_pseudo_user_id) {
        .id => |user_id| {
            if (!user_accounts.contains(user_id)) {
                log.warn("ignoring bogus userid: {}", .{user_id});
                return;
            }
            const account = try getUserAccountForEditing(changes, user_id);
            account.permissions = convertPermsissions(perms);
        },
        .guest => {
            guest_perms = convertPermsissions(perms);
            //changes.guest_perms = true;
            var it = user_accounts.iterator();
            while (it.next()) |kv| {
                const user_id = kv.key_ptr.*;
                const account = kv.value_ptr;
                if (account.registration_stage == .approved) continue;

                try changes.user_accounts.put(user_id, {});
                changes.broadcastChanges(.users);

                account.permissions = guest_perms;
            }
        },
    }
}

pub fn deleteUsers(changes: *db.Changes, user_ids: []const Id) !void {
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
                    replacement_guest_id = try createGuestAccount(changes);
                }
                it.promoteForEditing(changes, kv).value_ptr.user_id = replacement_guest_id.?;
            }
        }
        try events.tombstoneUser(changes, user_id);
        try deleteAccount(changes, user_id);
    }
}

fn createAccount(
    changes: *db.Changes,
    username_str: []const u8,
    password_hash: PasswordHash,
    registration_stage: RegistrationStage,
    permissions: InternalPermissions,
) !Id {
    try user_accounts.ensureUnusedCapacity(1);
    try changes.user_accounts.ensureUnusedCapacity(1);
    const username = try strings.put(g.gpa, username_str);

    const user_id = Id.random();
    const gop = user_accounts.getOrPutAssumeCapacity(user_id);
    if (gop.found_existing) @panic("unlikely"); // TODO: use generateIdAndPut() kinda thing.
    const user = gop.value_ptr;
    user.* = UserAccount{
        .username = username,
        .password_hash = password_hash,
        .registration_stage = registration_stage,
        .permissions = permissions,
    };
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
            it.promoteForEditing(changes, kv).value_ptr.user_id = true_user_id;
        }
    }
    try events.revealTrueIdentity(changes, doomed_user_id, true_user_id);
    try deleteAccount(changes, Id{ .value = workaround_miscomiplation });
}

/// all sessions should be migrated off this user id before calling this.
fn deleteAccount(changes: *db.Changes, user_id: Id) !void {
    try changes.user_accounts.put(user_id, {});
    changes.broadcastChanges(.users);
    std.debug.assert(user_accounts.swapRemove(user_id));
}

fn getUserAccount(user_id: Id) *const UserAccount {
    return user_accounts.getEntry(user_id).?.value_ptr;
}
fn getUserAccountForEditing(changes: *db.Changes, user_id: Id) !*UserAccount {
    const account = user_accounts.getEntry(user_id).?.value_ptr;
    try changes.user_accounts.put(user_id, {});
    changes.broadcastChanges(.users);
    return account;
}

pub fn getSerializableSessions(arena: Allocator, out_version: *?Id) !protocol.IdMap(protocol.Session) {
    // This version number never matters. Sessions beginning/ending changes the
    // hash, which means there's no scenario in which a newly connecting client
    // would get a cache hit on their previous version of the sessions. Delta
    // compression for live connections is still meaningful, so we don't want
    // this to stay null, but this number never does anything meaningful.
    out_version.* = Id.random();

    var result = protocol.IdMap(protocol.Session){};
    try result.map.ensureTotalCapacity(arena, sessions.table.count());

    var it = sessions.iterator();
    while (it.next()) |kv| {
        const id = kv.key_ptr.*;
        const session = kv.value_ptr;
        result.map.putAssumeCapacityNoClobber(id, .{
            .userId = session.user_id,
            .streaming = session.claims_to_be_streaming,
        });
    }

    return result;
}

pub fn getSerializableUsers(arena: Allocator, out_version: *?Id) !IdOrGuestMap(PublicUserInfo) {
    // TODO: meaningful versioning.
    out_version.* = Id.random();

    var result = IdOrGuestMap(PublicUserInfo){};
    try result.map.ensureTotalCapacity(arena, user_accounts.count() + 1);

    var it = user_accounts.iterator();
    while (it.next()) |kv| {
        const user_id = kv.key_ptr.*;
        const account = kv.value_ptr;
        result.map.putAssumeCapacityNoClobber(.{ .id = user_id }, .{
            .name = strings.get(account.username),
            .perms = convertPermsissions(account.permissions),
            .registration = switch (account.registration_stage) {
                .guest_without_password, .guest_with_password => .guest,
                .named_by_user => .named_by_user,
                .requested_approval => .requested_approval,
                .approved => .approved,
                _ => unreachable,
            },
        });
    }

    result.map.putAssumeCapacityNoClobber(.guest, .{
        // Displayed in the permission edit UI:
        .name = "Guests",
        // This is the important information:
        .perms = convertPermsissions(guest_perms),
        // This gets the pseudo user to show up in the permission edit UI:
        .registration = .approved,
    });

    return result;
}

fn convertPermsissions(other_permissions: anytype) if (@TypeOf(other_permissions) == Permissions) InternalPermissions else Permissions {
    return .{
        .read = other_permissions.read,
        .add = other_permissions.add,
        .control = other_permissions.control,
        .playlist = other_permissions.playlist,
        .admin = other_permissions.admin,
    };
}
