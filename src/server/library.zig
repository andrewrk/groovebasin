const std = @import("std");
const assert = std.debug.assert;
const ArrayList = std.ArrayList;
const Allocator = std.mem.Allocator;
const AutoArrayHashMap = std.AutoArrayHashMap;
const log = std.log;

const Groove = @import("groove.zig").Groove;
const g = @import("global.zig");

const StringPool = @import("StringPool.zig");

const LibraryTrack = @import("groovebasin_protocol.zig").LibraryTrack;
const Id = @import("groovebasin_protocol.zig").Id;
const IdMap = @import("groovebasin_protocol.zig").IdMap;

var current_version: Id = undefined;
var tracks: AutoArrayHashMap(Id, Track) = undefined;
var strings: StringPool = undefined;
var library_string_putter: StringPool.Putter = undefined;
var music_directory: []const u8 = undefined;

const Track = struct {
    file_path: StringPool.Index,
    title: StringPool.Index,
    artist: StringPool.OptionalIndex,
    composer: StringPool.OptionalIndex,
    performer: StringPool.OptionalIndex,
    album_artist: StringPool.OptionalIndex,
    album: StringPool.OptionalIndex,
    compilation: bool,
    track_number: ?i16,
    track_count: ?i16,
    disc_number: ?i16,
    disc_count: ?i16,
    duration: f64,
    year: ?i16,
    genre: StringPool.OptionalIndex,
};

pub fn init(music_directory_init: []const u8) !void {
    current_version = Id.random();
    tracks = AutoArrayHashMap(Id, Track).init(g.gpa);
    strings = StringPool.init(g.gpa);
    library_string_putter = strings.initPutter();
    music_directory = music_directory_init;
}

pub fn deinit() void {
    tracks.deinit();
    library_string_putter.deinit();
    strings.deinit();
}

pub fn loadFromDisk() !void {
    assert(.empty == try library_string_putter.putString(""));

    // TODO: update libgroove to support openat so we can store the music_dir fd
    // only and not do the absolute file concatenation below
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

        log.debug("found: {s}", .{full_path});

        try groove_file.open(full_path, full_path);
        defer groove_file.close();

        var it: ?*Groove.Tag = null;
        while (t: {
            it = groove_file.metadata_get("", it, 0);
            break :t it;
        }) |tag| {
            log.debug("  {s}={s}", .{ tag.key(), tag.value() });
        }
        const track = try grooveFileToTrack(&library_string_putter, groove_file, entry.path);
        try generateIdAndPut(&tracks, track);
    }
}

fn getMetadata(groove_file: *Groove.File, key: [*:0]const u8) ?[:0]const u8 {
    const tag = groove_file.metadata_get(key, null, 0) orelse return null;
    return std.mem.span(tag.value());
}

fn isCompilation(groove_file: *Groove.File, key: [*:0]const u8) bool {
    const s = getMetadata(groove_file, key) orelse return false;
    if (s.len == 0) return false;
    return s[0] != '0';
}

fn filenameWithoutExt(p: []const u8) []const u8 {
    const basename = std.fs.path.basename(p);
    const ext = std.fs.path.extension(basename);
    return basename[0 .. basename.len - ext.len];
}

fn trim(s: []const u8) []const u8 {
    return std.mem.trim(u8, s, " \r\n\t");
}

fn grooveFileToTrack(
    string_pool: *StringPool.Putter,
    groove_file: *Groove.File,
    file_path: []const u8,
) !Track {
    const parsed_track = parseTrackTuple(getMetadata(groove_file, "track") orelse
        "");
    const parsed_disc = parseTrackTuple(getMetadata(groove_file, "disc") orelse
        getMetadata(groove_file, "TPA") orelse
        getMetadata(groove_file, "TPOS") orelse
        "");
    return Track{
        .file_path = try string_pool.putString(file_path),

        .title = try string_pool.putString(trim(getMetadata(groove_file, "title") orelse
            filenameWithoutExt(file_path))),

        .artist = if (getMetadata(groove_file, "artist")) |s|
            (try string_pool.putString(trim(s))).toOptional()
        else
            .none,

        .composer = if (getMetadata(groove_file, "composer") orelse
            getMetadata(groove_file, "TCM")) |s| (try string_pool.putString(trim(s))).toOptional() else .none,

        .performer = if (getMetadata(groove_file, "performer")) |s| (try string_pool.putString(trim(s))).toOptional() else .none,
        .album_artist = if (getMetadata(groove_file, "album_artist")) |s| (try string_pool.putString(trim(s))).toOptional() else .none,
        .album = if (getMetadata(groove_file, "album")) |s| (try string_pool.putString(trim(s))).toOptional() else .none,

        .compilation = isCompilation(groove_file, "TCP") or
            isCompilation(groove_file, "TCMP") or
            isCompilation(groove_file, "COMPILATION") or
            isCompilation(groove_file, "Compilation") or
            isCompilation(groove_file, "cpil") or
            isCompilation(groove_file, "WM/IsCompilation"),

        .track_number = parsed_track.numerator,
        .track_count = parsed_track.denominator,
        .disc_number = parsed_disc.numerator,
        .disc_count = parsed_disc.denominator,

        .duration = groove_file.duration(),
        .year = if (getMetadata(groove_file, "date")) |s| (std.fmt.parseInt(i16, s, 10) catch null) else null,
        .genre = if (getMetadata(groove_file, "genre")) |s| (try string_pool.putString(trim(s))).toOptional() else .none,
    };
}

pub fn getSerializable(arena: Allocator, out_version: *?Id) !IdMap(LibraryTrack) {
    out_version.* = current_version;
    var result = IdMap(LibraryTrack){};
    try result.map.ensureTotalCapacity(arena, tracks.count());

    var it = tracks.iterator();
    while (it.next()) |kv| {
        const id = kv.key_ptr.*;
        const track = kv.value_ptr.*;
        result.map.putAssumeCapacityNoClobber(id, trackToSerializedForm(&strings, id, track));
    }
    return result;
}
fn trackToSerializedForm(string_pool: *StringPool, id: Id, track: Track) LibraryTrack {
    return .{
        .key = id,
        .file = string_pool.getString(track.file_path),
        .name = string_pool.getString(track.title),
        .artistName = string_pool.getOptionalString(track.artist) orelse "",
        .albumArtistName = string_pool.getOptionalString(track.album_artist) orelse "",
        .albumName = string_pool.getOptionalString(track.album) orelse "",
        .genre = string_pool.getOptionalString(track.genre) orelse "",
        .composerName = string_pool.getOptionalString(track.composer) orelse "",
        .performerName = string_pool.getOptionalString(track.performer) orelse "",
        .track = track.track_number orelse 0,
        .trackCount = track.track_count orelse 0,
        .disc = track.disc_number orelse 0,
        .discCount = track.disc_count orelse 0,
        .duration = track.duration,
        .compilation = track.compilation,
        .year = track.year orelse 0,
    };
}
const TrackTuple = struct {
    numerator: ?i16,
    denominator: ?i16,
};

fn parseTrackTuple(s: []const u8) TrackTuple {
    if (s.len == 0) return .{
        .numerator = null,
        .denominator = null,
    };

    if (std.mem.indexOfScalar(u8, s, '/')) |index| {
        const denom_string = s[index + 1 ..];
        return .{
            .numerator = std.fmt.parseInt(i16, s[0..index], 10) catch null,
            .denominator = if (denom_string.len == 0)
                null
            else
                std.fmt.parseInt(i16, denom_string, 10) catch null,
        };
    }

    return .{
        .numerator = std.fmt.parseInt(i16, s, 10) catch null,
        .denominator = null,
    };
}

test parseTrackTuple {
    const expectEqual = std.testing.expectEqual;
    try expectEqual(@as(?i16, null), parseTrackTuple("").numerator);
    try expectEqual(@as(?i16, null), parseTrackTuple("").denominator);

    try expectEqual(@as(?i16, 1), parseTrackTuple("1/100").numerator);
    try expectEqual(@as(?i16, 100), parseTrackTuple("1/100").denominator);

    try expectEqual(@as(?i16, null), parseTrackTuple("/-50").numerator);
    try expectEqual(@as(?i16, -50), parseTrackTuple("/-50").denominator);

    try expectEqual(@as(?i16, 10), parseTrackTuple("10").numerator);
    try expectEqual(@as(?i16, null), parseTrackTuple("10").denominator);
}

fn generateIdAndPut(map: anytype, value: anytype) !void {
    for (0..10) |_| {
        const gop = try map.getOrPut(Id.random());
        if (!gop.found_existing) {
            gop.value_ptr.* = value;
            return;
        }
        // This is a @setCold path. See https://github.com/ziglang/zig/issues/5177 .
        log.warn("Rerolling random id to avoid collisions", .{});
    }
    return error.FailedToGenerateRandomNumberAvoidingCollisions;
}

pub fn loadGrooveFile(library_key: Id) error{ OutOfMemory, TrackNotFound, LoadFailure }!*Groove.File {
    const groove_file = try g.groove.file_create();
    errdefer groove_file.destroy();

    const track = tracks.get(library_key) orelse return error.TrackNotFound;
    const file_path = strings.getString(track.file_path);

    const full_path = try std.fs.path.joinZ(g.gpa, &.{ music_directory, file_path });
    defer g.gpa.free(full_path);

    groove_file.open(full_path, full_path) catch |err| switch (err) {
        error.OutOfMemory => return error.OutOfMemory,
        else => {
            log.err("unable to open groove file '{s}': {s}", .{ full_path, @errorName(err) });
            return error.LoadFailure;
        },
    };
    errdefer groove_file.close();

    return groove_file;
}
