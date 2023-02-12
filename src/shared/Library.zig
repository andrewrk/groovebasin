const std = @import("std");
const AutoArrayHashMap = std.AutoArrayHashMap;

const StringPool = @import("StringPool.zig");
const Track = @import("protocol.zig").Track;

strings: StringPool,
tracks: AutoArrayHashMap(u64, Track),

pub fn init(allocator: std.mem.Allocator) @This() {
    return .{
        .strings = StringPool.init(allocator),
        .tracks = AutoArrayHashMap(u64, Track).init(allocator),
    };
}

pub fn deinit(l: *@This()) void {
    l.strings.strings.deinit();
    l.tracks.deinit();
    l.* = undefined;
}

pub fn getString(l: @This(), i: u32) [:0]const u8 {
    return l.strings.getString(i);
}
