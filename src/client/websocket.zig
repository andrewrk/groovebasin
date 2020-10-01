const browser = @import("browser.zig");
const callback = @import("callback.zig");

pub const onOpen_id = 0;
pub fn onOpen(context: *callback.Context) void {
    browser.print("zig: websocket opened");
}

pub const onClose_id = 1;
pub fn onClose(context: *callback.Context, arg: i32) void {
    browser.print("zig: websocket closed");
}
