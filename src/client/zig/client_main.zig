const std = @import("std");
const websocket_handler = @import("websocket_handler.zig");
const browser = @import("browser.zig");
const env = @import("browser_env.zig");
const ui = @import("groovebasin_ui.zig");

export fn main() void {
    browser.print("zig: hello world");
    ui.loadDomElements();

    websocket_handler.open();
}

pub fn panic(msg: []const u8, stacktrace: ?*std.builtin.StackTrace) noreturn {
    _ = stacktrace;
    env.panic(msg.ptr, msg.len);
    unreachable;
}
