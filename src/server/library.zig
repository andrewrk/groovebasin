const std = @import("std");
const assert = std.debug.assert;
const ArrayList = std.ArrayList;
const Allocator = std.mem.Allocator;
const AutoArrayHashMapUnmanaged = std.AutoArrayHashMapUnmanaged;
const log = std.log;

const Groove = @import("groove.zig").Groove;
const g = @import("global.zig");

const StringPool = @import("StringPool.zig");

const protocol = @import("groovebasin_protocol.zig");
const LibraryTrack = protocol.LibraryTrack;
const Id = protocol.Id;
const IdMap = protocol.IdMap;
const db = @import("db.zig");
const Track = db.Track;

const tracks = &g.the_database.tracks;
var music_directory: []const u8 = undefined;

pub fn init(music_directory_init: []const u8) !void {
    music_directory = music_directory_init;
}
pub fn deinit() void {}

pub fn loadFromDisk() !void {
    var arena = std.heap.ArenaAllocator.init(g.gpa);
    defer arena.deinit();

    var dont_care_about_this = db.Changes{};
    const changes = &dont_care_about_this;

    var path_to_id = AutoArrayHashMapUnmanaged(StringPool.Index, Id){};
    {
        var it = tracks.iterator();
        while (it.next()) |kv| {
            const gop = try path_to_id.getOrPut(arena.allocator(), kv.value_ptr.file_path);
            if (gop.found_existing) return error.DataCorruption; // db contains the same path multiple times.
            gop.value_ptr.* = kv.key_ptr.*;
        }
    }
    var found_ids = AutoArrayHashMapUnmanaged(Id, void){};

    // TODO: update libgroove to support openat so we can store the music_dir fd
    // only and not do the absolute file concatenation below
    var music_dir = try std.fs.cwd().openIterableDir(music_directory, .{});
    defer music_dir.close();

    var walker = try music_dir.walk(arena.allocator());
    defer walker.deinit();

    while (try walker.next()) |entry| {
        if (entry.kind != .file) continue;

        const groove_file = try g.groove.file_create();
        defer groove_file.destroy();

        const full_path = try std.fs.path.joinZ(arena.allocator(), &.{
            music_directory, entry.path,
        });
        defer arena.allocator().free(full_path);

        log.debug("found: {s}", .{full_path});

        try groove_file.open(full_path, full_path);
        defer groove_file.close();

        const track = try grooveFileToTrack(groove_file, entry.path);
        if (path_to_id.get(track.file_path)) |id| {
            const slot = try tracks.getForEditing(changes, id);
            slot.* = track;
            try found_ids.putNoClobber(arena.allocator(), id, {});
        } else {
            const id = try tracks.putRandom(changes, track);
            try found_ids.putNoClobber(arena.allocator(), id, {});
        }
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
        .file_path = try g.strings.put(g.gpa, file_path),

        .title = try g.strings.put(g.gpa, trim(getMetadata(groove_file, "title") orelse
            filenameWithoutExt(file_path))),

        .artist = if (getMetadata(groove_file, "artist")) |s|
            (try g.strings.put(g.gpa, trim(s))).toOptional()
        else
            .none,

        .composer = if (getMetadata(groove_file, "composer") orelse
            getMetadata(groove_file, "TCM")) |s| (try g.strings.put(g.gpa, trim(s))).toOptional() else .none,

        .performer = if (getMetadata(groove_file, "performer")) |s| (try g.strings.put(g.gpa, trim(s))).toOptional() else .none,
        .album_artist = if (getMetadata(groove_file, "album_artist")) |s| (try g.strings.put(g.gpa, trim(s))).toOptional() else .none,
        .album = if (getMetadata(groove_file, "album")) |s| (try g.strings.put(g.gpa, trim(s))).toOptional() else .none,

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
        .year = if (getMetadata(groove_file, "date")) |s| (std.fmt.parseInt(i16, s, 10) catch -1) else -1,
        .genre = if (getMetadata(groove_file, "genre")) |s| (try g.strings.put(g.gpa, trim(s))).toOptional() else .none,
    };
}

pub fn getSerializable(arena: Allocator, out_version: *?Id) !IdMap(LibraryTrack) {
    out_version.* = Id.random(); // TODO: versioning
    var result = IdMap(LibraryTrack){};
    try result.map.ensureTotalCapacity(arena, tracks.table.count());

    var it = tracks.iterator();
    while (it.next()) |kv| {
        const id = kv.key_ptr.*;
        const track = kv.value_ptr.*;
        result.map.putAssumeCapacityNoClobber(id, trackToSerializedForm(id, track));
    }
    return result;
}
fn trackToSerializedForm(id: Id, track: Track) LibraryTrack {
    return .{
        .key = id,
        .file = g.strings.get(track.file_path),
        .name = g.strings.get(track.title),
        .artistName = g.strings.getOptional(track.artist) orelse "",
        .albumArtistName = g.strings.getOptional(track.album_artist) orelse "",
        .albumName = g.strings.getOptional(track.album) orelse "",
        .genre = g.strings.getOptional(track.genre) orelse "",
        .composerName = g.strings.getOptional(track.composer) orelse "",
        .performerName = g.strings.getOptional(track.performer) orelse "",
        .track = intOrNull(track.track_number),
        .trackCount = intOrNull(track.track_count),
        .disc = intOrNull(track.disc_number),
        .discCount = intOrNull(track.disc_count),
        .duration = track.duration,
        .compilation = track.compilation,
        .year = intOrNull(track.year),
        .fingerprintScanStatus = track.fingerprint_scan_status,
        .loudnessScanStatus = track.loudness_scan_status,
    };
}
const TrackTuple = struct {
    numerator: i16,
    denominator: i16,
};

fn intOrNull(val: i16) ?u15 {
    if (val < 0) return null;
    return @intCast(val);
}

fn parseTrackTuple(s: []const u8) TrackTuple {
    if (s.len == 0) return .{
        .numerator = -1,
        .denominator = -1,
    };

    if (std.mem.indexOfScalar(u8, s, '/')) |index| {
        const denom_string = s[index + 1 ..];
        return .{
            .numerator = std.fmt.parseInt(i16, s[0..index], 10) catch -1,
            .denominator = if (denom_string.len == 0)
                -1
            else
                std.fmt.parseInt(i16, denom_string, 10) catch -1,
        };
    }

    return .{
        .numerator = std.fmt.parseInt(i16, s, 10) catch -1,
        .denominator = -1,
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

pub fn loadGrooveFile(library_key: Id) error{ OutOfMemory, TrackNotFound, LoadFailure }!*Groove.File {
    const groove_file = try g.groove.file_create();
    errdefer groove_file.destroy();

    const track = tracks.getOrNull(library_key) orelse return error.TrackNotFound;
    const file_path = g.strings.get(track.file_path);

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
