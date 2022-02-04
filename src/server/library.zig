const std = @import("std");
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;

const Groove = @import("groove.zig").Groove;
const g = @import("global.zig");

const protocol = @import("shared").protocol;
const Track = protocol.Track;

pub const Library = @import("shared").Library;
pub const StringPool = @import("shared").StringPool;

pub var current_library_version: u64 = 1;
pub var library: Library = undefined;

pub var music_dir_path: []const u8 = undefined;

pub fn init(music_directory: []const u8, db_path: []const u8) !void {
    music_dir_path = music_directory;
    // TODO: try reading from disk sometimes.
    // try readLibrary(db_path);

    library = Library{
        .strings = .{ .strings = ArrayList(u8).init(g.gpa) },
        .tracks = AutoArrayHashMap(u64, Track).init(g.gpa),
    };
    errdefer library.deinit();

    var music_dir = try std.fs.cwd().openDir(music_dir_path, .{ .iterate = true });
    defer music_dir.close();

    var walker = try music_dir.walk(g.gpa);
    defer walker.deinit();

    var id: u64 = 1;
    while (try walker.next()) |entry| {
        if (entry.kind != .File) continue;

        const groove_file = try g.groove.file_create();
        defer groove_file.destroy();

        const full_path = try std.fs.path.joinZ(g.gpa, &.{
            music_dir_path, entry.path,
        });
        defer g.gpa.free(full_path);

        std.log.debug("found: {s}", .{full_path});

        try groove_file.open(full_path, full_path);
        defer groove_file.close();

        var it: ?*Groove.Tag = null;
        while (t: {
            it = groove_file.metadata_get("", it, 0);
            break :t it;
        }) |tag| {
            std.log.debug("  {s}={s}", .{ tag.key(), tag.value() });
        }
        try library.tracks.putNoClobber(id, try grooveFileToTrack(&library.strings, groove_file, entry.path));
        id += 1;
    }

    try writeLibrary(db_path);
}

fn writeLibrary(db_path: []const u8) !void {
    var db_file = try std.fs.cwd().createFile(db_path, .{});
    defer db_file.close();

    const header = Header{
        .string_size = @intCast(u32, library.strings.strings.items.len),
        .track_count = @intCast(u32, library.tracks.count()),
    };

    // if we try to get library.tracks.keys().ptr when the count is zero, it
    // ends up being a null pointer
    if (library.tracks.count() == 0)
        return;

    var iovecs = [_]std.os.iovec_const{
        .{
            .iov_base = @ptrCast([*]const u8, &header),
            .iov_len = @sizeOf(Header),
        },
        .{
            .iov_base = library.strings.strings.items.ptr,
            .iov_len = library.strings.strings.items.len,
        },
        .{
            .iov_base = @ptrCast([*]const u8, library.tracks.keys().ptr),
            .iov_len = library.tracks.keys().len * @sizeOf(u64),
        },
        .{
            .iov_base = @ptrCast([*]const u8, library.tracks.values().ptr),
            .iov_len = library.tracks.values().len * @sizeOf(Track),
        },
    };

    try db_file.writevAll(&iovecs);
}

pub fn deinit() void {
    library.deinit();
}

fn readLibrary(db_path: []const u8) anyerror!void {
    var l = Library{
        .strings = .{ .strings = ArrayList(u8).init(g.gpa) },
        .tracks = AutoArrayHashMap(u64, Track).init(g.gpa),
    };
    defer l.deinit();

    var db_file = try std.fs.cwd().openFile(db_path, .{});
    defer db_file.close();

    const header = try db_file.reader().readStruct(Header);

    try l.strings.strings.resize(header.string_size);
    try l.tracks.ensureTotalCapacity(header.track_count);
    const track_keys = try g.gpa.alloc(u64, header.track_count);
    defer g.gpa.free(track_keys);
    const track_values = try g.gpa.alloc(Track, header.track_count);
    defer g.gpa.free(track_values);

    var iovecs = [_]std.os.iovec{
        .{
            .iov_base = l.strings.strings.items.ptr,
            .iov_len = l.strings.strings.items.len,
        },
        .{
            .iov_base = @ptrCast([*]u8, track_keys.ptr),
            .iov_len = track_keys.len * @sizeOf(u64),
        },
        .{
            .iov_base = @ptrCast([*]u8, track_values.ptr),
            .iov_len = track_values.len * @sizeOf(Track),
        },
    };
    _ = try db_file.readvAll(&iovecs);

    for (track_keys) |k, i| {
        l.tracks.putAssumeCapacityNoClobber(k, track_values[i]);
    }
}

const Header = extern struct {
    string_size: u32,
    track_count: u32,
};

fn grooveFileToTrack(
    string_pool: *StringPool,
    groove_file: *Groove.File,
    file_path: []const u8,
) !Track {
    // ported from https://github.com/andrewrk/groovebasin/blob/07dd1ee01d77beb901d8b9adeaf21c5f7030c70f/lib/player.js#L2850-L2888
    // TODO reserve index 0 for null strings and use that instead of fake data (e.g. "(No Title)")
    return Track{
        .file_path = try string_pool.putString(file_path),
        .title = try string_pool.putString(if (groove_file.metadata_get("title", null, 0)) |tag|
            std.mem.span(tag.value())
        else
            "(No Title)"),
        .artist = try string_pool.putString(if (groove_file.metadata_get("artist", null, 0)) |tag|
            std.mem.span(tag.value())
        else
            "(No Artist)"),
        .album = try string_pool.putString(if (groove_file.metadata_get("album", null, 0)) |tag|
            std.mem.span(tag.value())
        else
            "(No Album)"),
    };
}
