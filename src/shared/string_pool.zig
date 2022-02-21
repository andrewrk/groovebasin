const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const HashMapUnmanaged = std.HashMapUnmanaged;

pub const StringPool = struct {
    allocator: Allocator,
    bytes: ArrayListUnmanaged(u8),
    deduplication_table: ?DeduplicationTable,

    pub fn init(allocator: Allocator) @This() {
        return @This(){
            .allocator = allocator,
            .bytes = .{},
            .deduplication_table = DeduplicationTable{},
        };
    }
    pub fn initSizeImmutable(allocator: Allocator, size: usize) !@This() {
        var bytes = ArrayListUnmanaged(u8){};
        try bytes.resize(allocator, size);
        return @This(){
            .allocator = allocator,
            .bytes = bytes,
            .deduplication_table = null,
        };
    }
    pub fn deinit(self: *@This()) void {
        self.bytes.deinit(self.allocator);
        if (self.deduplication_table) |*x| {
            x.deinit(self.allocator);
        }
        self.* = undefined;
    }

    pub fn putString(self: *@This(), s: []const u8) !u32 {
        // Check if we've already got it.
        if (self.deduplication_table.?.getKeyAdapted(s, self.deduplicationAdapter())) |index| {
            return index;
        }

        // Insert a new entry.
        const index = @intCast(u32, self.bytes.items.len);
        try self.bytes.ensureUnusedCapacity(self.allocator, s.len + 1);
        self.bytes.appendSliceAssumeCapacity(s);
        self.bytes.appendAssumeCapacity(0);
        try self.deduplication_table.?.putNoClobberContext(self.allocator, index, {}, self.deduplicationContext());
        return index;
    }

    pub fn getString(self: @This(), i: u32) [:0]const u8 {
        return std.mem.span(self.getStringZ(i));
    }
    pub fn getStringZ(self: @This(), i: u32) [*:0]const u8 {
        return @ptrCast([*:0]const u8, &self.bytes.items[i]);
    }

    const DeduplicationTable = HashMapUnmanaged(u32, void, StringIndexContext, std.hash_map.default_max_load_percentage);
    fn deduplicationContext(self: *@This()) StringIndexContext {
        return .{
            .bytes = &self.bytes,
        };
    }
    fn deduplicationAdapter(self: *@This()) StringIndexAdapter {
        return .{
            .bytes = &self.bytes,
        };
    }
};

// Copied from std.hash_map.
// (As of writing this, this type is unreferenced in the std lib, so I don't trust its stability.)
const StringIndexContext = struct {
    bytes: *std.ArrayListUnmanaged(u8),

    pub fn eql(self: @This(), a: u32, b: u32) bool {
        _ = self;
        return a == b;
    }

    pub fn hash(self: @This(), x: u32) u64 {
        const x_slice = std.mem.sliceTo(@ptrCast([*:0]const u8, self.bytes.items.ptr) + x, 0);
        return std.hash_map.hashString(x_slice);
    }
};

const StringIndexAdapter = struct {
    bytes: *std.ArrayListUnmanaged(u8),
    pub fn hash(self: @This(), k: []const u8) u64 {
        _ = self;
        return std.hash_map.hashString(k);
    }
    pub fn eql(self: @This(), k: []const u8, k2: u32) bool {
        const k2_slice = std.mem.sliceTo(@ptrCast([*:0]const u8, self.bytes.items.ptr) + k2, 0);
        return std.mem.eql(u8, k, k2_slice);
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
