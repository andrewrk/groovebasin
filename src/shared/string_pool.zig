const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const StringHashMapUnmanaged = std.StringHashMapUnmanaged;

pub const StringPool = struct {
    allocator: Allocator,
    strings: ArrayListUnmanaged(u8),
    deduplication_table: ?StringHashMapUnmanaged(u32),

    pub fn init(allocator: Allocator) @This() {
        return @This(){
            .allocator = allocator,
            .strings = .{},
            .deduplication_table = StringHashMapUnmanaged(u32){},
        };
    }
    pub fn initSizeImmutable(allocator: Allocator, size: usize) !@This() {
        var strings = ArrayListUnmanaged(u8){};
        try strings.resize(allocator, size);
        return @This(){
            .allocator = allocator,
            .strings = strings,
            .deduplication_table = null,
        };
    }
    pub fn deinit(self: *@This()) void {
        self.strings.deinit(self.allocator);
        if (self.deduplication_table) |*x| {
            x.deinit(self.allocator);
        }
        self.* = undefined;
    }

    pub fn putString(self: *@This(), s: []const u8) !u32 {
        // Check if we've already got it.
        if (self.deduplication_table.?.get(s)) |index| {
            return index;
        }

        // Insert a new entry.
        const index = @intCast(u32, self.strings.items.len);
        try self.strings.ensureUnusedCapacity(self.allocator, s.len + 1);
        self.strings.appendSliceAssumeCapacity(s);
        self.strings.appendAssumeCapacity(0);
        try self.deduplication_table.?.putNoClobber(self.allocator, s, index);
        return index;
    }

    pub fn getString(self: @This(), i: u32) [:0]const u8 {
        return std.mem.span(self.getStringZ(i));
    }
    pub fn getStringZ(self: @This(), i: u32) [*:0]const u8 {
        return @ptrCast([*:0]const u8, &self.strings.items[i]);
    }
};

test "StringPool" {
    var s = StringPool.init(std.testing.allocator);
    defer s.deinit();

    // basics
    const foo = try s.putString("foo");
    try std.testing.expectEqualStrings(s.getString(foo), "foo");
    const bar = try s.putString("bar");
    try std.testing.expectEqualStrings(s.getString(bar), "bar");

    // deduplication
    try std.testing.expectEqual(try s.putString("foo"), foo);

    // more basics
    const baz = try s.putString("baz");
    try std.testing.expectEqualStrings(s.getString(foo), "foo");
    try std.testing.expectEqualStrings(s.getString(bar), "bar");
    try std.testing.expectEqualStrings(s.getString(baz), "baz");
}
