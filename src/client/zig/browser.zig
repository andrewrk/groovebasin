// A convenient abstraction for essential functions.
const env = @import("browser_env.zig");

const protocol = @import("shared").protocol;

pub fn print(str: []const u8) void {
    env.print(str.ptr, str.len);
}

pub fn readBlob(handle: i32, buf: []u8) void {
    env.readBlob(handle, buf.ptr, buf.len);
}

pub fn getTime() protocol.Timestamp {
    const milliseconds = env.getTime();
    return protocol.Timestamp{
        .s = @divTrunc(milliseconds, 1000),
        .ns = @intCast(i32, @mod(milliseconds, 1000) * 1_000_000),
    };
}
