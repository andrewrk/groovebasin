const StringPool = @This();

const std = @import("std");
const Allocator = std.mem.Allocator;

buf: std.ArrayListUnmanaged(u8) = .{},
dedup_table: std.HashMapUnmanaged(Index, void, Context, 20) = .{},

pub const Index = enum(u32) {
    empty,
    _,

    pub fn toOptional(self: @This()) OptionalIndex {
        return @enumFromInt(@intFromEnum(self));
    }
};

pub const OptionalIndex = enum(u32) {
    empty,
    none = std.math.maxInt(u32),
    _,

    pub fn unwrap(self: @This()) ?Index {
        if (self == .none) return null;
        return @enumFromInt(@intFromEnum(self));
    }
};

pub fn deinit(self: *@This(), allocator: Allocator) void {
    self.buf.deinit(allocator);
    self.dedup_table.deinit(allocator);
    self.* = undefined;
}

pub fn len(self: @This()) u32 {
    return @intCast(self.buf.items.len);
}

pub fn get(self: @This(), i: Index) [:0]const u8 {
    // Couldn't figure out how to use std.mem.span() here.
    const bytes = self.buf.items;
    var end = @intFromEnum(i);
    while (bytes[end] != 0) end += 1;
    return bytes[@intFromEnum(i)..end :0];
}

pub fn getOptional(self: @This(), optional_index: OptionalIndex) ?[:0]const u8 {
    return get(self, optional_index.unwrap() orelse return null);
}

pub fn put(self: *@This(), allocator: Allocator, s: []const u8) !Index {
    try self.ensureUnusedCapacity(allocator, 1, s.len);
    return self.putAssumeCapacity(s);
}

pub fn putAssumeCapacity(self: *@This(), s: []const u8) Index {
    const gop = self.dedup_table.getOrPutAssumeCapacityAdapted(
        s,
        AdaptedContext{ .pool = self },
    );
    if (gop.found_existing) return gop.key_ptr.*;

    const index: Index = @enumFromInt(self.buf.items.len);
    self.buf.appendSliceAssumeCapacity(s);
    self.buf.appendAssumeCapacity(0);
    gop.key_ptr.* = index;

    return index;
}

pub fn ensureUnusedCapacity(
    self: *@This(),
    allocator: Allocator,
    number_of_strings: usize,
    total_string_len_not_including_sentinels: usize,
) !void {
    const buf_space_needed = total_string_len_not_including_sentinels + number_of_strings; // 1 sentinel per string.
    try self.buf.ensureUnusedCapacity(allocator, @intCast(buf_space_needed));
    try self.dedup_table.ensureUnusedCapacityContext(allocator, @intCast(number_of_strings), Context{ .pool = self });
}

pub fn eql(self: @This(), index: Index, slice: []const u8) bool {
    for (slice, @intFromEnum(index)..) |c, i| {
        if (c != self.buf.items[i]) return false;
    }
    return self.buf.items[slice.len] == 0;
}

/// Call this after directly modifying `buf`.
/// This rebuilds the deduplication table.
pub fn reindex(self: *@This(), allocator: Allocator) !void {
    self.dedup_table.clearRetainingCapacity();
    var start: usize = 0;
    for (self.buf.items, 0..) |c, i| {
        if (c != 0) continue;
        try self.dedup_table.putNoClobberContext(allocator, @enumFromInt(start), {}, Context{ .pool = self });
        start = i + 1;
    }
    std.debug.assert(start == self.buf.items.len);
}

const Context = struct {
    pool: *const StringPool,
    pub fn hash(self: @This(), k: Index) u64 {
        return std.hash.Wyhash.hash(0, self.pool.get(k));
    }
    pub fn eql(_: @This(), a: Index, b: Index) bool {
        return a == b;
    }
};
const AdaptedContext = struct {
    pool: *const StringPool,
    pub fn hash(_: @This(), k: []const u8) u64 {
        return std.hash.Wyhash.hash(0, k);
    }
    pub fn eql(self: @This(), a: []const u8, b: Index) bool {
        return self.pool.eql(b, a);
    }
};
