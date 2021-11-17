const env = @import("browser_env.zig");

pub fn print(str: []const u8) void {
    env.print(str.ptr, str.len);
}

pub const openWebSocket = env.openWebSocket;

pub fn readBlob(handle: i32, buf: []u8) void {
    env.readBlob(handle, buf.ptr, buf.len);
}

pub fn sendMessage(handle: i32, buf: []const u8) void {
    env.sendMessage(handle, buf.ptr, buf.len);
}
