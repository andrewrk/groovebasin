const browser = @import("browser.zig");
const callback = @import("callback.zig");

pub fn onOpen(context: *callback.Context) void {
    browser.print("zig: websocket opened");
}

pub fn onClose(context: *callback.Context, arg: i32) void {
    browser.print("zig: websocket closed");
}
