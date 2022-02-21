const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;
const Allocator = std.mem.Allocator;

const StringPool = @import("string_pool.zig").StringPool;
const Track = @import("protocol.zig").Track;

pub const Library = struct {
    strings: StringPool,
    tracks: AutoArrayHashMap(u64, Track),

    pub fn init(allocator: Allocator) @This() {
        return @This(){
            .strings = StringPool.init(allocator),
            .tracks = AutoArrayHashMap(u64, Track).init(allocator),
        };
    }

    pub fn deinit(self: *@This()) void {
        self.strings.deinit();
        self.tracks.deinit();
        self.* = undefined;
    }

    pub fn putTrack(self: *@This(), strings: StringPool, track_id: u64, track: Track) !void {
        try self.tracks.put(
            track_id,
            Track{
                .file_path = try self.strings.putString(strings.getString(track.file_path)),
                .title = try self.strings.putString(strings.getString(track.title)),
                .artist = try self.strings.putString(strings.getString(track.artist)),
                .album = try self.strings.putString(strings.getString(track.album)),
            },
        );
    }

    pub fn getStringZ(self: @This(), index: u32) [*:0]const u8 {
        return self.strings.getStringZ(index);
    }
    pub fn getString(self: @This(), index: u32) [:0]const u8 {
        return self.strings.getString(index);
    }
};
