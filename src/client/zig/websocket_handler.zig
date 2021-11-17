const browser = @import("browser.zig");
const callback = @import("callback.zig");
const ui = @import("groovebasin_ui.zig");

var websocket_handle: i32 = undefined;

pub fn onOpen(context: *callback.Context, handle: i32) void {
    _ = context;
    browser.print("zig: websocket opened");
    ui.setLoadingState(.good);

    websocket_handle = handle;

    // try it out!
    browser.sendMessage(handle, "ping");
}

pub fn onClose(context: *callback.Context, code: i32) void {
    _ = context;
    _ = code;
    browser.print("zig: websocket closed");
    ui.setLoadingState(.no_connection);
}

pub fn onError(context: *callback.Context) void {
    _ = context;
    browser.print("zig: websocket error");
    ui.setLoadingState(.no_connection);
}

pub fn onMessage(context: *callback.Context, handle: i32, _len: i32) void {
    _ = context;
    const len = @intCast(usize, _len);

    var buffer: [0x1000]u8 = undefined;
    browser.readBlob(handle, buffer[0..len]);

    browser.print(buffer[0..len]);
}

pub fn sendMessage(message: []const u8) void {
    browser.sendMessage(websocket_handle, message);
}
