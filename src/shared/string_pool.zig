const std = @import("std");
const ArrayList = std.ArrayList;

pub const StringPool = struct {
    strings: ArrayList(u8),

    pub fn putString(self: *@This(), s: []const u8) !u32 {
        const index = @intCast(u32, self.strings.items.len);
        try self.strings.ensureUnusedCapacity(s.len + 1);
        self.strings.appendSliceAssumeCapacity(s);
        self.strings.appendAssumeCapacity(0);
        return index;
    }

    pub fn getString(self: *@This(), i: u32) [*:0]const u8 {
        return @ptrCast([*:0]const u8, &self.strings.items[i]);
    }
};
