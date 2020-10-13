const browser = @import("browser.zig");
const callback = @import("callback.zig");

var websocket_handle: i32 = undefined;

pub fn onOpen(context: *callback.Context, handle: i32) void {
    browser.print("zig: websocket opened");

    websocket_handle = handle;

    // try it out!
    browser.sendMessage(handle, "ping");
}

pub fn onClose(context: *callback.Context, code: i32) void {
    browser.print("zig: websocket closed");
}

pub fn onError(context: *callback.Context) void {
    browser.print("zig: websocket error");
}

pub fn onMessage(context: *callback.Context, handle: i32, _len: i32) void {
    const len = @intCast(usize, _len);

    var buffer: [0x1000]u8 = undefined;
    browser.readBlob(handle, buffer[0..len]);

    browser.print(buffer[0..len]);

    // let's talk a lot.
    browser.sendMessage(websocket_handle, "pong");
}

pub fn sendMessage(message: []const u8) void {
    browser.sendMessage(websocket_handle, message);
}
