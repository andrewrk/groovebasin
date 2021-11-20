// A convenient abstraction for essential functions.
const env = @import("browser_env.zig");
const g = @import("global.zig");

pub fn print(str: []const u8) void {
    env.print(str.ptr, str.len);
}

/// FIXME: fix std.log formatting instead of doing this.
pub fn printHex(buf: []const u8) void {
    const formatted = g.gpa.alloc(u8, buf.len * 2) catch |err| {
        @panic(@errorName(err));
    };
    defer g.gpa.free(formatted);

    for (buf) |b, i| {
        formatted[i * 2 + 0] = "0123456789abcdef"[b >> 4];
        formatted[i * 2 + 1] = "0123456789abcdef"[b & 0b1111];
    }
    print(formatted);
}

pub fn readBlob(handle: i32, buf: []u8) void {
    env.readBlob(handle, buf.ptr, buf.len);
}
