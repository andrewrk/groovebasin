const std = @import("std");
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const Allocator = std.mem.Allocator;

pub const StringPool = struct {
    allocator: Allocator,
    strings: ArrayListUnmanaged(u8),

    pub fn init(allocator: Allocator) @This() {
        return @This(){
            .allocator = allocator,
            .strings = .{},
        };
    }
    pub fn initSize(allocator: Allocator, size: usize) !@This() {
        var strings = ArrayListUnmanaged(u8){};
        try strings.resize(allocator, size);
        return @This(){
            .allocator = allocator,
            .strings = strings,
        };
    }
    pub fn deinit(self: *@This()) void {
        self.strings.deinit(self.allocator);
        self.* = undefined;
    }

    pub fn putString(self: *@This(), s: []const u8) !u32 {
        const index = @intCast(u32, self.strings.items.len);
        try self.strings.ensureUnusedCapacity(self.allocator, s.len + 1);
        self.strings.appendSliceAssumeCapacity(s);
        self.strings.appendAssumeCapacity(0);
        return index;
    }

    pub fn getString(self: @This(), i: u32) [:0]const u8 {
        return std.mem.span(self.getStringZ(i));
    }
    pub fn getStringZ(self: @This(), i: u32) [*:0]const u8 {
        return @ptrCast([*:0]const u8, &self.strings.items[i]);
    }
};
