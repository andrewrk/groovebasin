const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;

const StringPool = @import("string_pool.zig").StringPool;
const Track = @import("protocol.zig").Track;

pub const Library = struct {
    strings: StringPool,
    tracks: AutoArrayHashMap(u64, Track),

    /// The returned slice is invalidated when any strings are added to the string table.
    pub fn getString(l: Library, index: u32) [:0]const u8 {
        const bytes = l.strings.strings.items;
        var end: usize = index;
        while (bytes[end] != 0) end += 1;
        return bytes[index..end :0];
    }

    pub fn deinit(l: *Library) void {
        l.strings.strings.deinit();
        l.tracks.deinit();
        l.* = undefined;
    }
};
