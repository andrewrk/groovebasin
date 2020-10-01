const env = @import("env.zig");

pub fn print(str: []const u8) void {
    env.print(str.ptr, str.len);
}

pub const serveWebSocket = env.serveWebSocket;
