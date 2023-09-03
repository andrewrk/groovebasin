const std = @import("std");
const Allocator = std.mem.Allocator;
const ArenaAllocator = std.heap.ArenaAllocator;
const log = std.log;
const mem = std.mem;

const g = @import("global.zig");
const library = @import("library.zig");
const queue = @import("queue.zig");
const events = @import("events.zig");
const keese = @import("keese.zig");
const subscriptions = @import("subscriptions.zig");
const users = @import("users.zig");
const db = @import("db.zig");

const Groove = @import("groove.zig").Groove;
const SoundIo = @import("soundio.zig").SoundIo;
const Player = @import("Player.zig");

const groovebasin_protocol = @import("groovebasin_protocol.zig");
const Id = groovebasin_protocol.Id;
const web_server = @import("web_server.zig");

const Config = @import("Config.zig");

const usage =
    \\Usage: groovebasin [options]
    \\Options:
    \\  --config [file]      Defaults to config.json in the cwd
    \\  -h, --help           Print this help menu to stdout
    \\
;

pub const std_options = struct {
    // std.log configuration.
    //pub const log_level = .info;
};

pub fn fatal(comptime format: []const u8, args: anytype) noreturn {
    log.err(format, args);
    std.process.exit(1);
}

pub fn main() anyerror!void {
    var gpa_instance: std.heap.GeneralPurposeAllocator(.{}) = .{};
    defer _ = gpa_instance.deinit();
    g.gpa = gpa_instance.allocator();

    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    const arena = arena_instance.allocator();

    const args = try std.process.argsAlloc(arena);

    var config_json_path: []const u8 = "config.json";
    var i: usize = 1;
    while (i < args.len) : (i += 1) {
        const arg = args[i];
        if (mem.eql(u8, arg, "--config")) {
            if (i + 1 >= args.len) fatal("expected parameter after {s}", .{arg});
            i += 1;
            config_json_path = args[i];
        } else if (mem.eql(u8, arg, "-h") or mem.eql(u8, arg, "--help")) {
            try std.io.getStdOut().writeAll(usage);
            std.process.exit(0);
        } else {
            fatal("unrecognized argument: '{s}'", .{arg});
        }
    }

    var parsed_config = try Config.loadOrInitAndExit(config_json_path);
    defer parsed_config.deinit();
    const config = parsed_config.value;

    // ================
    // startup sequence
    // ================

    const music_dir_path = config.musicDirectory orelse try Config.defaultMusicPath(arena);
    const addr = std.net.Address.parseIp(config.host, config.port) catch |err| {
        fatal("unable to parse {s}:{d}: {s}", .{ config.host, config.port, @errorName(err) });
    };

    log.info("music directory: {s}", .{music_dir_path});

    log.debug("libgroove version: {s}", .{Groove.version()});

    const soundio = try SoundIo.create();
    g.soundio = soundio;
    g.soundio.app_name = "GrooveBasin";
    g.soundio.connect_backend(.Dummy) catch |err| fatal("unable to initialize sound: {s}", .{@errorName(err)});
    g.soundio.flush_events();

    const groove = try Groove.create();
    Groove.set_logging(.INFO);
    g.groove = groove;

    g.player = try Player.init(config.encodeBitRate);
    // TODO: fix the deinit bug in libgroove
    //defer g.player.deinit();

    try g.strings.ensureConstants(g.gpa);
    try db.init();
    defer db.deinit();
    try subscriptions.init();
    defer subscriptions.deinit();
    try keese.init(g.gpa);
    defer keese.deinit();
    try library.init(music_dir_path);
    defer library.deinit();
    try queue.init();
    defer queue.deinit();
    try events.init();
    defer events.deinit();

    log.info("load db", .{});
    try db.load(config.dbPath);

    try library.loadFromDisk();
    try queue.handleLoaded();

    log.info("init static content", .{});
    {
        // TODO: resolve relative to current executable maybe?
        var static_content_dir = try std.fs.cwd().openDir("zig-out/lib/public", .{});
        defer static_content_dir.close();
        try web_server.initStaticContent(static_content_dir, &[_][]const u8{
            "/",
            "/app.js",
            "/favicon.png",
            "/img/bright-10.png",
            "/img/ui-bg_diagonals-thick_15_0b3e6f_40x40.png",
            "/img/ui-bg_dots-medium_30_0b58a2_4x4.png",
            "/img/ui-bg_dots-small_20_333333_2x2.png",
            "/img/ui-bg_dots-small_30_a32d00_2x2.png",
            "/img/ui-bg_dots-small_40_00498f_2x2.png",
            "/img/ui-bg_flat_0_aaaaaa_40x100.png",
            "/img/ui-bg_flat_40_292929_40x100.png",
            "/img/ui-bg_gloss-wave_20_111111_500x100.png",
            "/img/ui-icons_00498f_256x240.png",
            "/img/ui-icons_98d2fb_256x240.png",
            "/img/ui-icons_9ccdfc_256x240.png",
            "/img/ui-icons_ffffff_256x240.png",
        });
    }

    try web_server.spawnListenThread(addr);

    web_server.mainLoop();
    unreachable;
}

pub fn handleClientConnected(client_id: Id) !void {
    var arena = ArenaAllocator.init(g.gpa);
    defer arena.deinit();
    var changes = db.Changes{};

    // Welcome messages
    try encodeAndSend(client_id, .{ .time = getNow() });
    const err_maybe = users.handleClientConnected(&changes, client_id);

    try changes.flush(arena.allocator());
    return err_maybe;
}
pub fn handleClientDisconnected(client_id: Id) !void {
    var arena = ArenaAllocator.init(g.gpa);
    defer arena.deinit();
    var changes = db.Changes{};

    subscriptions.handleClientDisconnected(client_id);
    const err_maybe = users.handleClientDisconnected(&changes, client_id);

    try changes.flush(arena.allocator());
    return err_maybe;
}

fn parseMessage(allocator: Allocator, message_bytes: []const u8) !groovebasin_protocol.ClientToServerMessage {
    var scanner = std.json.Scanner.initCompleteInput(allocator, message_bytes);
    defer scanner.deinit();
    var diagnostics = std.json.Diagnostics{};
    scanner.enableDiagnostics(&diagnostics);
    return std.json.parseFromTokenSourceLeaky(
        groovebasin_protocol.ClientToServerMessage,
        allocator,
        &scanner,
        .{},
    ) catch |err| {
        log.warn("json err: {}: line,col: {},{}", .{ err, diagnostics.getLine(), diagnostics.getColumn() });
        return err;
    };
}

pub fn encodeAndSend(client_id: Id, message: groovebasin_protocol.ServerToClientMessage) !void {
    const message_bytes = try std.json.stringifyAlloc(g.gpa, message, .{});
    try web_server.sendMessageToClient(client_id, message_bytes);
}

pub fn handleRequest(client_id: Id, message_bytes: []const u8) !void {
    var arena = ArenaAllocator.init(g.gpa);
    defer arena.deinit();
    const message = try parseMessage(arena.allocator(), message_bytes);

    var changes = db.Changes{};

    const err_maybe = handleRequestImpl(&changes, client_id, &message);

    try changes.flush(arena.allocator());
    return err_maybe;
}

fn handleRequestImpl(changes: *db.Changes, client_id: Id, message: *const groovebasin_protocol.ClientToServerMessage) !void {
    const user_id = users.getUserId(client_id);
    const perms = users.getPermissions(user_id);
    switch (message.*) {
        .login => |args| {
            try users.login(changes, client_id, args.username, args.password);
        },
        .logout => {
            try users.logout(changes, client_id);
        },
        .ensureAdminUser => {
            try users.ensureAdminUser(changes);
        },
        .requestApproval => {
            try users.requestApproval(changes, client_id);
        },
        .approve => |args| {
            try checkPermission(perms.admin);
            try users.approve(changes, args);
        },
        .updateUser => |args| {
            try checkPermission(perms.admin);
            try users.updateUser(changes, args.userId, args.perms);
        },
        .updateGuestPermissions => |args| {
            try checkPermission(perms.admin);
            try users.updateGuestPermissions(changes, args);
        },
        .deleteUsers => |args| {
            try checkPermission(perms.admin);
            try users.deleteUsers(changes, args);
        },
        .setStreaming => |args| {
            try users.setStreaming(changes, client_id, args);
        },

        .subscribe => |args| {
            try checkPermission(perms.read);
            try subscriptions.subscribe(client_id, args.name, args.delta, args.version);
        },

        .queue => |args| {
            try checkPermission(perms.control);
            try queue.enqueue(changes, args);
        },
        .move => |args| {
            try checkPermission(perms.control);
            try queue.move(changes, args);
        },
        .remove => |args| {
            try checkPermission(perms.control);
            try queue.remove(changes, args);
        },

        .chat => |args| {
            try checkPermission(perms.control);
            try events.chat(changes, user_id, args.text, args.displayClass != null);
        },
        .deleteTracks => @panic("TODO"),
        .autoDjOn => @panic("TODO"),
        .autoDjHistorySize => @panic("TODO"),
        .autoDjFutureSize => @panic("TODO"),
        .hardwarePlayback => @panic("TODO"),
        .importNames => @panic("TODO"),
        .importUrl => @panic("TODO"),
        .updateTags => @panic("TODO"),
        .unsubscribe => @panic("TODO"),
        .pause => {
            try checkPermission(perms.control);
            try queue.pause(changes, user_id);
        },
        .play => {
            try checkPermission(perms.control);
            try queue.play(changes, user_id);
        },
        .seek => |args| {
            try checkPermission(perms.control);
            try queue.seek(changes, user_id, args.id, args.pos);
        },
        .repeat => @panic("TODO"),
        .setVolume => @panic("TODO"),
        .stop => @panic("TODO"),
        .playlistCreate => @panic("TODO"),
        .playlistRename => @panic("TODO"),
        .playlistDelete => @panic("TODO"),
        .playlistAddItems => @panic("TODO"),
        .playlistRemoveItems => @panic("TODO"),
        .playlistMoveItems => @panic("TODO"),
        .labelCreate => @panic("TODO"),
        .labelRename => @panic("TODO"),
        .labelColorUpdate => @panic("TODO"),
        .labelDelete => @panic("TODO"),
        .labelAdd => @panic("TODO"),
        .labelRemove => @panic("TODO"),
        .lastFmGetSession => @panic("TODO"),
        .lastFmScrobblersAdd => @panic("TODO"),
        .lastFmScrobblersRemove => @panic("TODO"),
    }
}

fn checkPermission(has_permission: bool) !void {
    if (!has_permission) return error.PermissionDenied;
}

pub fn getNow() groovebasin_protocol.Datetime {
    return .{ .value = std.time.milliTimestamp() };
}
