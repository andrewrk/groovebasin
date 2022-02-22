sections: u8,
strings: StringPool,
footer_bytes: ArrayList(u8),

const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;

const g = @import("global.zig");
const protocol = @import("shared").protocol;
const StringPool = @import("shared").StringPool;

pub fn init(allocator: Allocator, sections: u8) @This() {
    return @This(){
        .sections = sections,
        .strings = StringPool.init(allocator),
        .footer_bytes = ArrayList(u8).init(allocator),
    };
}

pub fn deinit(self: *@This()) void {
    self.strings.deinit();
    self.footer_bytes.deinit();
    self.* = undefined;
}

pub fn writer(self: *@This()) ArrayList(u8).Writer {
    return self.footer_bytes.writer();
}

pub fn build(self: *@This()) ![]const u8 {
    var buf = ArrayList(u8).init(self.strings.allocator);
    try buf.writer().writeStruct(protocol.ResponseHeader{
        .seq_id = 0x8000_0000,
    });
    try buf.writer().writeStruct(protocol.PushMessageHeader{
        .string_size = @intCast(u32, self.strings.bytes.items.len),
        .sections = self.sections,
    });
    try buf.writer().writeAll(self.strings.bytes.items);
    try buf.writer().writeAll(self.footer_bytes.items);

    defer self.deinit();
    return buf.toOwnedSlice();
}
