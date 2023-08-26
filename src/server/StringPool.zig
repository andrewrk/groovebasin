const StringPool = @This();

const std = @import("std");

buf: std.ArrayList(u8),

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

pub fn init(allocator: std.mem.Allocator) @This() {
    return .{
        .buf = std.ArrayList(u8).init(allocator),
    };
}
pub fn deinit(self: *@This()) void {
    self.buf.deinit();
    self.* = undefined;
}

pub fn getString(self: @This(), i: Index) [:0]const u8 {
    // Couldn't figure out how to use std.mem.span() here.
    const bytes = self.buf.items;
    var end = @intFromEnum(i);
    while (bytes[end] != 0) end += 1;
    return bytes[@intFromEnum(i)..end :0];
}

pub fn getOptionalString(self: @This(), optional_index: OptionalIndex) ?[:0]const u8 {
    return getString(self, optional_index.unwrap() orelse return null);
}

pub fn putWithoutDeduplication(self: *@This(), s: []const u8) !Index {
    try self.ensureUnusedCapacity(s.len);
    return self.putWithoutDeduplicationAssumeCapacity(s);
}
pub fn putWithoutDeduplicationAssumeCapacity(self: *@This(), s: []const u8) Index {
    const index: Index = @enumFromInt(self.buf.items.len);
    self.buf.appendSliceAssumeCapacity(s);
    self.buf.appendAssumeCapacity(0);
    return index;
}
pub fn ensureUnusedCapacity(self: *@This(), string_len: usize) !void {
    try self.buf.ensureUnusedCapacity(string_len + 1);
}

pub fn initPutter(self: *@This()) Putter {
    return Putter.init(self);
}

pub const Putter = struct {
    pool: *StringPool,
    dedup_table: std.HashMapUnmanaged(Index, void, Context, 20),

    pub fn init(pool: *StringPool) Putter {
        return .{
            .pool = pool,
            .dedup_table = .{},
        };
    }

    pub fn deinit(self: *@This()) void {
        // we don't own self.pool.
        self.dedup_table.deinit(self.pool.buf.allocator);
    }

    pub fn putString(self: *@This(), s: []const u8) !Index {
        try self.pool.ensureUnusedCapacity(s.len);
        const gop = try self.dedup_table.getOrPutContextAdapted(
            self.pool.buf.allocator,
            s,
            AdaptedContext{ .pool = self.pool },
            Context{ .pool = self.pool },
        );

        if (gop.found_existing) return gop.key_ptr.*;

        const index = self.pool.putWithoutDeduplicationAssumeCapacity(s);
        gop.key_ptr.* = index;

        return index;
    }

    const Context = struct {
        pool: *const StringPool,
        pub fn hash(self: @This(), k: Index) u64 {
            return std.hash.Wyhash.hash(0, self.pool.getString(k));
        }
        pub fn eql(_: @This(), _: Index, _: Index) bool {
            unreachable; // unused.
        }
    };
    const AdaptedContext = struct {
        pool: *const StringPool,
        pub fn hash(_: @This(), k: []const u8) u64 {
            return std.hash.Wyhash.hash(0, k);
        }
        pub fn eql(self: @This(), a: []const u8, b: Index) bool {
            return std.mem.eql(u8, a, self.pool.getString(b));
        }
    };
};
