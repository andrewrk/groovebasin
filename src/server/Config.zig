// These are JSON-compatible types.
host: []const u8 = "127.0.0.1",
port: u16 = 16242,
dbPath: []const u8 = "groovebasin.db",
musicDirectory: ?[]const u8 = null,
lastFmApiKey: []const u8 = "bb9b81026cd44fd086fa5533420ac9b4",
lastFmApiSecret: []const u8 = "2309a40ae3e271de966bf320498a8f09",
acoustidAppKey: []const u8 = "bgFvC4vW",
encodeQueueDuration: f64 = 8,
encodeBitRate: u32 = 256,
sslKey: ?[]const u8 = null,
sslCert: ?[]const u8 = null,
sslCaDir: ?[]const u8 = null,
googleApiKey: []const u8 = "AIzaSyDdTDD8-gu_kp7dXtT-53xKcVbrboNAkpM",
ignoreExtensions: []const []const u8 = &.{
    ".jpg", ".jpeg", ".txt", ".png", ".log", ".cue", ".pdf", ".m3u",
    ".nfo", ".ini",  ".xml", ".zip",
},

const std = @import("std");
const fs = std.fs;
const json = std.json;
const log = std.log;
const Allocator = std.mem.Allocator;

const g = @import("global.zig");
const fatal = @import("server_main.zig").fatal;

pub fn loadOrInitAndExit(path: []const u8) !json.Parsed(@This()) {
    const file = fs.cwd().openFile(path, .{}) catch |err| switch (err) {
        error.FileNotFound => {
            var arena = std.heap.ArenaAllocator.init(g.gpa);
            defer arena.deinit();

            var atomic_file = try fs.cwd().atomicFile(path, .{});
            defer atomic_file.deinit();

            var buffered_writer = std.io.bufferedWriter(atomic_file.file.writer());

            try json.stringify(@This(){
                .musicDirectory = try defaultMusicPath(arena.allocator()),
            }, .{
                .whitespace = .indent_4,
            }, buffered_writer.writer());

            try buffered_writer.writer().writeByte('\n');

            try buffered_writer.flush();
            try atomic_file.finish();

            fatal("No {s} found; writing default. Take a peek and make sure the values are to your liking, then start GrooveBasin again.", .{path});
        },
        else => |e| {
            fatal("Unable to read {s}: {s}", .{ path, @errorName(e) });
        },
    };

    var reader = json.reader(g.gpa, file.reader());
    defer reader.deinit();
    var diagnostics = json.Diagnostics{};
    reader.enableDiagnostics(&diagnostics);
    return json.parseFromTokenSource(@This(), g.gpa, &reader, .{}) catch |err| {
        log.err("{s}:{}:{}: {s}", .{ path, diagnostics.getLine(), diagnostics.getColumn(), @errorName(err) });
        return err;
    };
}

pub fn defaultMusicPath(arena: Allocator) ![]const u8 {
    if (std.os.getenvZ("XDG_MUSIC_DIR")) |xdg_path| return xdg_path;

    if (std.os.getenv("HOME")) |home| {
        return try fs.path.join(arena, &.{ home, "music" });
    }

    return "music";
}
