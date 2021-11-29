const std = @import("std");
const websocket_handler = @import("websocket_handler.zig");
const browser = @import("browser.zig");
const env = @import("browser_env.zig");
const ui = @import("groovebasin_ui.zig");
const stream = @import("stream.zig");

export fn main() void {
    _ = async mainAsync();
}

fn mainAsync() void {
    browser.print("zig: hello world");
    ui.init();
    stream.init();

    websocket_handler.open();
}

pub fn panic(msg: []const u8, stacktrace: ?*std.builtin.StackTrace) noreturn {
    _ = stacktrace;
    env.panic(msg.ptr, msg.len);
    unreachable;
}
