const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;

const Groove = @import("groove.zig").Groove;
const g = @import("global.zig");

const Library = struct {
    strings: ArrayList(u8),
    tracks: AutoArrayHashMap(u64, Track),
};

const Track = struct {
    title: u32,
    artist: u32,
    album: u32,
};

fn putString(strings: *ArrayList(u8), s: [:0]const u8) !u32 {
    const index = @intCast(u32, strings.items.len);
    try strings.appendSlice(s[0 .. s.len + 1]);
    return index;
}

fn getString(strings: *ArrayList(u8), i: u32) [*:0]const u8 {
    return @ptrCast([*:0]const u8, &strings.items[i]);
}

pub fn libraryMain(music_directory: []const u8) anyerror!void {
    var l = Library{
        .strings = ArrayList(u8).init(g.gpa),
        .tracks = AutoArrayHashMap(u64, Track).init(g.gpa),
    };
    defer {
        l.strings.deinit();
        l.tracks.deinit();
    }

    var music_dir = try std.fs.cwd().openDir(music_directory, .{ .iterate = true });
    defer music_dir.close();

    var walker = try music_dir.walk(g.gpa);
    defer walker.deinit();

    var id: u64 = 1;
    while (try walker.next()) |entry| {
        if (entry.kind != .File) continue;

        const groove_file = try g.groove.file_create();
        defer groove_file.destroy();

        const full_path = try std.fs.path.joinZ(g.gpa, &.{
            music_directory, entry.path,
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
        try l.tracks.putNoClobber(id, try grooveFileToTrack(&l.strings, groove_file));
        id += 1;
    }

    // Serialize.
    var db_file = try std.fs.cwd().createFile("db.bin", .{});
    defer db_file.close();

    const header = Header{
        .string_size = @intCast(u32, l.strings.items.len),
        .track_count = @intCast(u32, l.tracks.count()),
    };

    var iovecs = [_]std.os.iovec_const{
        .{
            .iov_base = @ptrCast([*]const u8, &header),
            .iov_len = @sizeOf(Header),
        },
        .{
            .iov_base = l.strings.items.ptr,
            .iov_len = l.strings.items.len,
        },
        .{
            .iov_base = @ptrCast([*]const u8, l.tracks.keys().ptr),
            .iov_len = l.tracks.keys().len * @sizeOf(u64),
        },
        .{
            .iov_base = @ptrCast([*]const u8, l.tracks.values().ptr),
            .iov_len = l.tracks.values().len * @sizeOf(Track),
        },
    };
    try db_file.writevAll(&iovecs);

    try readLibrary();
}

fn readLibrary() anyerror!void {
    var l = Library{
        .strings = ArrayList(u8).init(g.gpa),
        .tracks = AutoArrayHashMap(u64, Track).init(g.gpa),
    };
    defer {
        l.strings.deinit();
        l.tracks.deinit();
    }

    var db_file = try std.fs.cwd().openFile("db.bin", .{});
    defer db_file.close();

    const header = try db_file.reader().readStruct(Header);

    try l.strings.resize(header.string_size);
    try l.tracks.ensureTotalCapacity(header.track_count);
    const track_keys = try g.gpa.alloc(u64, header.track_count);
    defer g.gpa.free(track_keys);
    const track_values = try g.gpa.alloc(Track, header.track_count);
    defer g.gpa.free(track_values);

    var iovecs = [_]std.os.iovec{
        .{
            .iov_base = l.strings.items.ptr,
            .iov_len = l.strings.items.len,
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

    // Should be good now?
    {
        const track = l.tracks.get(1).?;
        std.log.info(
            "track: {s} - {s} - {s}",
            .{
                getString(&l.strings, track.title),
                getString(&l.strings, track.artist),
                getString(&l.strings, track.album),
            },
        );
    }
    {
        const track = l.tracks.get(2).?;
        std.log.info(
            "track: {s} - {s} - {s}",
            .{
                getString(&l.strings, track.title),
                getString(&l.strings, track.artist),
                getString(&l.strings, track.album),
            },
        );
    }
}

const Header = extern struct {
    string_size: u32,
    track_count: u32,
};

fn grooveFileToTrack(strings: *ArrayList(u8), groove_file: *Groove.File) !Track {
    // ported from https://github.com/andrewrk/groovebasin/blob/07dd1ee01d77beb901d8b9adeaf21c5f7030c70f/lib/player.js#L2850-L2888
    return Track{
        .title = try putString(strings, if (groove_file.metadata_get("title", null, 0)) |tag|
            std.mem.span(tag.value())
        else
            "(No Title)"),
        .artist = try putString(strings, if (groove_file.metadata_get("artist", null, 0)) |tag|
            std.mem.span(tag.value())
        else
            "(No Artist)"),
        .album = try putString(strings, if (groove_file.metadata_get("album", null, 0)) |tag|
            std.mem.span(tag.value())
        else
            "(No Album)"),
    };
}
