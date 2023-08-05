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
const Allocator = std.mem.Allocator;

const fatal = @import("server_main.zig").fatal;

pub fn loadOrInitAndExit(arena: Allocator, path: []const u8) !@This() {
    const max_config_size = 1 * 1024 * 1024;
    const json_text = fs.cwd().readFileAlloc(arena, path, max_config_size) catch |err| switch (err) {
        error.FileNotFound => {
            var atomic_file = try fs.cwd().atomicFile(path, .{});
            defer atomic_file.deinit();

            var buffered_writer = std.io.bufferedWriter(atomic_file.file.writer());

            try json.stringify(@This(){
                .musicDirectory = try defaultMusicPath(arena),
            }, .{}, buffered_writer.writer());

            try buffered_writer.flush();
            try atomic_file.finish();

            fatal("No {s} found; writing default. Take a peek and make sure the values are to your liking, then start GrooveBasin again.", .{path});
        },
        else => |e| {
            fatal("Unable to read {s}: {s}", .{ path, @errorName(e) });
        },
    };
    return try json.parseFromSliceLeaky(@This(), arena, json_text, .{});
}

pub fn defaultMusicPath(arena: Allocator) ![]const u8 {
    if (std.os.getenvZ("XDG_MUSIC_DIR")) |xdg_path| return xdg_path;

    if (std.os.getenv("HOME")) |home| {
        return try fs.path.join(arena, &.{ home, "music" });
    }

    return "music";
}
