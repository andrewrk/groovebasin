const StringPool = @This();

const std = @import("std");

buf: std.ArrayList(u8),

pub fn init(allocator: std.mem.Allocator) @This() {
    return .{
        .buf = std.ArrayList(u8).init(allocator),
    };
}
pub fn deinit(self: *@This()) void {
    self.buf.deinit();
    self.* = undefined;
}

pub fn getString(self: @This(), i: u32) [:0]const u8 {
    // Couldn't figure out how to use std.mem.span() here.
    const bytes = self.buf.items;
    var end: usize = i;
    while (bytes[end] != 0) end += 1;
    return bytes[i..end :0];
}

pub fn putWithoutDeduplication(self: *@This(), s: []const u8) !u32 {
    const index = @as(u32, @intCast(self.buf.items.len));
    try self.buf.ensureUnusedCapacity(s.len + 1);
    self.buf.appendSliceAssumeCapacity(s);
    self.buf.appendAssumeCapacity(0);
    return index;
}

pub fn initPutter(self: *@This()) Putter {
    return Putter.init(self);
}

pub const Putter = struct {
    pool: *StringPool,
    dedup_table: std.HashMapUnmanaged(u32, void, Context, 20),

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

    pub fn putString(self: *@This(), s: []const u8) !u32 {
        const gop = try self.dedup_table.getOrPutContextAdapted(
            self.pool.buf.allocator,
            s,
            AdaptedContext{ .pool = self.pool },
            Context{ .pool = self.pool },
        );

        if (gop.found_existing) return gop.key_ptr.*;

        const index = try self.pool.putWithoutDeduplication(s);
        gop.key_ptr.* = index;

        return index;
    }

    const Context = struct {
        pool: *const StringPool,
        pub fn hash(self: @This(), k: u32) u64 {
            return std.hash.Wyhash.hash(0, self.pool.getString(k));
        }
        pub fn eql(_: @This(), _: u32, _: u32) bool {
            unreachable; // unused.
        }
    };
    const AdaptedContext = struct {
        pool: *const StringPool,
        pub fn hash(_: @This(), k: []const u8) u64 {
            return std.hash.Wyhash.hash(0, k);
        }
        pub fn eql(self: @This(), a: []const u8, b: u32) bool {
            return std.mem.eql(u8, a, self.pool.getString(b));
        }
    };
};
