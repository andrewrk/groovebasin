const std = @import("std");
const Allocator = std.mem.Allocator;
const ArenaAllocator = std.heap.ArenaAllocator;
const log = std.log;
const mem = std.mem;

const library = @import("library.zig");
const Queue = @import("queue.zig");
const events = @import("events.zig");
const subscriptions = @import("subscriptions.zig");
const users = @import("users.zig");
const db = @import("db.zig");
const StaticHttpFileServer = @import("StaticHttpFileServer");

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

pub fn fatal(comptime format: []const u8, args: anytype) noreturn {
    log.err(format, args);
    std.process.exit(1);
}

pub fn main() anyerror!noreturn {
    const g = @import("global.zig");

    var gpa_instance: std.heap.GeneralPurposeAllocator(.{}) = .{};
    defer _ = gpa_instance.deinit();
    const gpa = gpa_instance.allocator();
    g.gpa = gpa;

    var arena_instance = std.heap.ArenaAllocator.init(gpa);
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

    var queue: Queue = .{};
    g.queue = &queue;

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

    try g.strings.ensureConstants(gpa);
    try db.init();
    defer db.deinit();
    try subscriptions.init();
    defer subscriptions.deinit();
    try library.init(music_dir_path);
    defer library.deinit();
    try events.init();
    defer events.deinit();

    log.info("load db", .{});
    try db.load(config.dbPath);

    try library.loadFromDisk();

    try queue.handleLoaded();

    // The DB may have changed due to loadFromDisk.
    try db.flushChanges();

    log.info("init static content", .{});
    var static_http_file_server = s: {
        // TODO: this path needs to be a CLI flag, env var, found relative to the executable,
        // configured at build time, or some combination of the above.
        const static_asset_path = "zig-out/lib/public";

        var dir = std.fs.cwd().openDir(static_asset_path, .{ .iterate = true }) catch |err| {
            fatal("unable to open static asset directory '{s}': {s}", .{
                static_asset_path, @errorName(err),
            });
        };
        defer dir.close();

        break :s StaticHttpFileServer.init(.{
            .allocator = gpa,
            .root_dir = dir,
        }) catch |err| fatal("unable to init static asset server: {s}", .{@errorName(err)});
    };
    defer static_http_file_server.deinit(gpa);

    const thread = try std.Thread.spawn(.{}, web_server.listen, .{
        addr, &static_http_file_server,
    });
    defer thread.join();

    web_server.mainLoop();
}

pub fn handleClientConnected(client_id: Id) !void {
    // Welcome messages
    try encodeAndSend(client_id, .{ .time = getNow() });
    const err_maybe = users.handleClientConnected(client_id);

    try db.flushChanges();
    return err_maybe;
}
pub fn handleClientDisconnected(client_id: Id) !void {
    subscriptions.handleClientDisconnected(client_id);
    const err_maybe = users.handleClientDisconnected(client_id);

    try db.flushChanges();
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
    const g = @import("global.zig");
    const message_bytes = try std.json.stringifyAlloc(g.gpa, message, .{});
    try sendBytes(client_id, message_bytes);
}
/// Takes ownership of message_bytes, even when an error is returned.
pub fn sendBytes(client_id: Id, message_bytes: []const u8) !void {
    try web_server.sendMessageToClient(client_id, message_bytes);
}

pub fn handleRequest(client_id: Id, message_bytes: []const u8) !void {
    const g = @import("global.zig");
    var arena = ArenaAllocator.init(g.gpa);
    defer arena.deinit();
    const message = try parseMessage(arena.allocator(), message_bytes);

    const err_maybe = handleRequestImpl(client_id, &message);

    try db.flushChanges();
    return err_maybe;
}

fn handleRequestImpl(client_id: Id, message: *const groovebasin_protocol.ClientToServerMessage) !void {
    const g = @import("global.zig");
    const user_id = users.getUserId(client_id);
    const perms = users.getPermissions(user_id);
    switch (message.*) {
        .login => |args| {
            try users.login(client_id, args.username, args.password);
        },
        .logout => {
            try users.logout(client_id);
        },
        .ensureAdminUser => {
            try users.ensureAdminUser();
        },
        .requestApproval => {
            try users.requestApproval(client_id);
        },
        .approve => |args| {
            try checkPermission(perms.admin);
            try users.approve(args);
        },
        .updateUser => |args| {
            try checkPermission(perms.admin);
            try users.updateUser(args.userId, args.perms);
        },
        .updateGuestPermissions => |args| {
            try checkPermission(perms.admin);
            try users.updateGuestPermissions(args);
        },
        .deleteUsers => |args| {
            try checkPermission(perms.admin);
            try users.deleteUsers(args);
        },
        .setStreaming => |args| {
            try users.setStreaming(client_id, args);
        },

        .subscribe => |args| {
            try checkPermission(perms.read);
            try subscriptions.subscribe(client_id, args.name, args.delta, args.version);
        },

        .queue => |args| {
            try checkPermission(perms.control);
            try g.queue.enqueue(args);
        },
        .move => |args| {
            try checkPermission(perms.control);
            try g.queue.move(args);
        },
        .remove => |args| {
            try checkPermission(perms.control);
            try g.queue.remove(args);
        },

        .chat => |args| {
            try checkPermission(perms.control);
            try events.chat(user_id, args.text, args.displayClass != null);
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
            try g.queue.pause(user_id);
        },
        .play => {
            try checkPermission(perms.control);
            try g.queue.play(user_id);
        },
        .stop => {
            try checkPermission(perms.control);
            try g.queue.stop(user_id);
        },
        .seek => |args| {
            try checkPermission(perms.control);
            try g.queue.seek(user_id, args.id, args.pos);
        },
        .repeat => @panic("TODO"),
        .setVolume => @panic("TODO"),
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
