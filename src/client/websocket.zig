const browser = @import("browser.zig");
const callback = @import("callback.zig");

pub fn onOpen(context: *callback.Context) void {
    browser.print("zig: websocket opened");
}

pub fn onClose(context: *callback.Context, arg: i32) void {
    browser.print("zig: websocket closed");
}

pub fn onError(context: *callback.Context) void {
    browser.print("zig: websocket error");
}

pub fn onMessage(context: *callback.Context, handle: i32, _len: i32) void {
    const len = @intCast(usize, _len);
    browser.print("zig: handling message");

    var buffer: [0x1000]u8 = undefined;
    browser.readBlob(handle, buffer[0..len]);

    browser.print(buffer[0..len]);
}
