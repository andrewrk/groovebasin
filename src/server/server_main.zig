const std = @import("std");
const Allocator = std.mem.Allocator;
const ArenaAllocator = std.heap.ArenaAllocator;
const log = std.log;
const mem = std.mem;

const g = @import("global.zig");
const library = @import("library.zig");
const queue = @import("queue.zig");
const events = @import("events.zig");

const Groove = @import("groove.zig").Groove;
const SoundIo = @import("soundio.zig").SoundIo;
const Player = @import("Player.zig");

const groovebasin_protocol = @import("groovebasin_protocol.zig");
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
    defer g.player.deinit();

    log.info("init library", .{});
    try library.init(music_dir_path, config.dbPath);
    defer library.deinit();

    log.info("init queue", .{});
    try queue.init();
    defer queue.deinit();

    log.info("init events", .{});
    try events.init();
    defer events.deinit();

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

pub fn handleClientConnected(client_id: *anyopaque) !void {
    _ = client_id;
}
pub fn handleClientDisconnected(client_id: *anyopaque) !void {
    _ = client_id;
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

fn encodeAndSend(client_id: *anyopaque, message: groovebasin_protocol.ServerToClientMessage) !void {
    const message_bytes = try std.json.stringifyAlloc(g.gpa, message, .{});
    try web_server.sendMessageToClient(client_id, message_bytes);
}

pub fn handleRequest(client_id: *anyopaque, message_bytes: []const u8) !void {
    var arena = ArenaAllocator.init(g.gpa);
    defer arena.deinit();
    const message = try parseMessage(arena.allocator(), message_bytes);
    // startup messages:
    //  .user
    //  .time
    //  .token
    //  .lastFmApiKey
    //  .user (again, this time as not a guest)
    //  .streamEndpoint
    //  .autoDjOn
    //  .hardwarePlayback
    //  .haveAdminUser
    //  .labels - large database
    //  .library - large database
    //  .queue - large database
    //  .scanning
    //  .volume
    //  .repeat
    //  .currentTrack
    //  .playlists - large database
    //  .anonStreamers
    //  .users (not to be confused with .user)
    //  .events - large database
    //  .importProgress
    switch (message) {
        .setStreaming => {
            // TODO
        },
        .subscribe => |args| {
            var sub: groovebasin_protocol.Subscription = switch (args.name) {
                .library => .{
                    .library = try library.getSerializable(arena.allocator()),
                },
                else => return, // TODO: support more subscription streams.
            };

            // Not actually doing versioning. Just wrap in the appropriate structure.
            if (args.delta) {
                try encodeAndSend(client_id, .{ .subscription = .{
                    .sub = sub,
                    .delta_version = "delta-versioning-still-TODO",
                } });
            } else {
                try encodeAndSend(client_id, .{ .subscription = .{ .sub = sub } });
            }
        },
        else => unreachable,
    }
}
