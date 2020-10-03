const env = @import("browser_env.zig");

pub fn print(str: []const u8) void {
    env.print(str.ptr, str.len);
}

pub const serveWebSocket = env.serveWebSocket;

pub fn readBlob(handle: i32, buf: []u8) void {
    env.readBlob(handle, buf.ptr, buf.len);
}
