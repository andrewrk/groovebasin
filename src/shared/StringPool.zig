const std = @import("std");

strings: std.ArrayList(u8),

pub fn init(allocator: std.mem.Allocator) @This() {
    return .{
        .strings = std.ArrayList(u8).init(allocator),
    };
}
pub fn deinit(self: *@This()) void {
    self.strings.deinit();
    self.* = undefined;
}

pub fn putString(self: *@This(), s: []const u8) !u32 {
    const index = @intCast(u32, self.strings.items.len);
    try self.strings.ensureUnusedCapacity(s.len + 1);
    self.strings.appendSliceAssumeCapacity(s);
    self.strings.appendAssumeCapacity(0);
    return index;
}

pub fn getString(self: @This(), i: u32) [:0]const u8 {
    return std.mem.span(self.strings.items[i.. :0]);
}
