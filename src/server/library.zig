const std = @import("std");
const ArrayList = std.ArrayList;
const Allocator = std.mem.Allocator;
const AutoArrayHashMap = std.AutoArrayHashMap;

const Groove = @import("groove.zig").Groove;
const g = @import("global.zig");

const Library = @import("shared").Library;
const StringPool = @import("shared").StringPool;

const LibraryTrack = @import("groovebasin_protocol.zig").LibraryTrack;
const Id = @import("groovebasin_protocol.zig").Id;
const IdMap = @import("groovebasin_protocol.zig").IdMap;

pub var current_library_version: Id = undefined;
var tracks: AutoArrayHashMap(Id, Track) = undefined;
var strings: StringPool = undefined;
var library_string_putter: StringPool.Putter = undefined;

pub const Track = extern struct {
    file_path: u32,
    title: u32,
    artist: u32,
    album: u32,
    track_number: i16,
};

pub fn init(music_directory: []const u8, db_path: []const u8) !void {
    // TODO: try reading from disk sometimes.
    // try readLibrary(db_path);

    current_library_version = Id.random();
    tracks = AutoArrayHashMap(Id, Track).init(g.gpa);
    errdefer tracks.deinit();
    strings = StringPool.init(g.gpa);
    errdefer strings.deinit();
    library_string_putter = strings.initPutter();
    errdefer library_string_putter.deinit();

    var music_dir = try std.fs.cwd().openIterableDir(music_directory, .{});
    defer music_dir.close();

    var walker = try music_dir.walk(g.gpa);
    defer walker.deinit();

    while (try walker.next()) |entry| {
        if (entry.kind != .file) continue;

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
        const track = try grooveFileToTrack(&library_string_putter, groove_file, entry.path);
        try generateIdAndPut(&tracks, track);
    }

    try writeLibrary(db_path);
}

fn writeLibrary(db_path: []const u8) !void {
    var db_file = try std.fs.cwd().createFile(db_path, .{});
    defer db_file.close();

    const header = Header{
        .string_size = @as(u32, @intCast(strings.buf.items.len)),
        .track_count = @as(u32, @intCast(tracks.count())),
    };

    // if we try to get tracks.keys().ptr when the count is zero, it
    // ends up being a null pointer
    if (tracks.count() == 0)
        return;

    var iovecs = [_]std.os.iovec_const{
        .{
            .iov_base = @as([*]const u8, @ptrCast(&header)),
            .iov_len = @sizeOf(Header),
        },
        .{
            .iov_base = strings.buf.items.ptr,
            .iov_len = strings.buf.items.len,
        },
        .{
            .iov_base = @as([*]const u8, @ptrCast(tracks.keys().ptr)),
            .iov_len = tracks.keys().len * @sizeOf(u64),
        },
        .{
            .iov_base = @as([*]const u8, @ptrCast(tracks.values().ptr)),
            .iov_len = tracks.values().len * @sizeOf(Track),
        },
    };

    try db_file.writevAll(&iovecs);
}

pub fn deinit() void {
    strings.deinit();
    tracks.deinit();
}

fn readLibrary(db_path: []const u8) anyerror!void {
    var l = Library.init(g.gpa);
    defer l.deinit();

    var db_file = try std.fs.cwd().openFile(db_path, .{});
    defer db_file.close();

    const header = try db_file.reader().readStruct(Header);

    try l.strings.buf.resize(header.string_size);
    try l.tracks.ensureTotalCapacity(header.track_count);
    const track_keys = try g.gpa.alloc(u64, header.track_count);
    defer g.gpa.free(track_keys);
    const track_values = try g.gpa.alloc(Track, header.track_count);
    defer g.gpa.free(track_values);

    var iovecs = [_]std.os.iovec{
        .{
            .iov_base = l.strings.buf.items.ptr,
            .iov_len = l.strings.buf.items.len,
        },
        .{
            .iov_base = @as([*]u8, @ptrCast(track_keys.ptr)),
            .iov_len = track_keys.len * @sizeOf(u64),
        },
        .{
            .iov_base = @as([*]u8, @ptrCast(track_values.ptr)),
            .iov_len = track_values.len * @sizeOf(Track),
        },
    };
    _ = try db_file.readvAll(&iovecs);

    for (track_keys, track_values) |k, v| {
        l.tracks.putAssumeCapacityNoClobber(k, v);
    }
}

const Header = extern struct {
    string_size: u32,
    track_count: u32,
};

fn grooveFileToTrack(
    string_pool: *StringPool.Putter,
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
        .track_number = if (groove_file.metadata_get("track", null, 0)) |tag|
            parseTrackNumber(std.mem.span(tag.value()))
        else
            0,
    };
}

pub fn getSerializable(arena: Allocator) !IdMap(LibraryTrack) {
    var result = IdMap(LibraryTrack){};

    var it = tracks.iterator();
    while (it.next()) |kv| {
        const id = kv.key_ptr.*;
        const track = kv.value_ptr.*;
        try result.map.putNoClobber(arena, id, trackToSerializedForm(&strings, id, track));
    }
    return result;
}
fn trackToSerializedForm(string_pool: *StringPool, id: Id, track: Track) LibraryTrack {
    return .{
        .key = id,
        .file = string_pool.getString(track.file_path),
        .name = string_pool.getString(track.title),
        .artistName = string_pool.getString(track.artist),
        .albumName = string_pool.getString(track.album),
        .track = track.track_number,
    };
}

fn parseTrackNumber(value: []const u8) i16 {
    const numerator = if (std.mem.indexOfScalar(u8, value, '/')) |index|
        value[0..index]
    else
        value;
    return std.fmt.parseInt(i16, numerator, 10) catch 0;
}

fn generateIdAndPut(map: anytype, value: anytype) !void {
    for (0..10) |_| {
        const gop = try map.getOrPut(Id.random());
        if (!gop.found_existing) {
            gop.value_ptr.* = value;
            return;
        }
        // This is a @setCold path. See https://github.com/ziglang/zig/issues/5177 .
        std.log.warn("Rerolling random id to avoid collisions", .{});
    }
    return error.FailedToGenerateRandomNumberAvoidingCollisions;
}
