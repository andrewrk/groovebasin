const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;

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

pub fn libraryMain(gpa: *Allocator) anyerror!void {
    var l = Library{
        .strings = ArrayList(u8).init(gpa),
        .tracks = AutoArrayHashMap(u64, Track).init(gpa),
    };
    defer {
        l.strings.deinit();
        l.tracks.deinit();
    }

    // Some data.
    try l.tracks.putNoClobber(1, Track{
        .title = try putString(&l.strings, "Sightseeing In The Apocalypse"),
        .artist = try putString(&l.strings, "Diablo Swing Orchestra"),
        .album = try putString(&l.strings, "Swagger & Stroll Down The Rabbit Hole"),
    });
    try l.tracks.putNoClobber(2, Track{
        .title = try putString(&l.strings, "War Painted Valentine"),
        .artist = try putString(&l.strings, "Diablo Swing Orchestra"),
        .album = try putString(&l.strings, "Swagger & Stroll Down The Rabbit Hole"),
    });

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

    try readLibrary(gpa);
}

fn readLibrary(gpa: *Allocator) anyerror!void {
    var l = Library{
        .strings = ArrayList(u8).init(gpa),
        .tracks = AutoArrayHashMap(u64, Track).init(gpa),
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
    const track_keys = try gpa.alloc(u64, header.track_count);
    defer gpa.free(track_keys);
    const track_values = try gpa.alloc(Track, header.track_count);
    defer gpa.free(track_values);

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
