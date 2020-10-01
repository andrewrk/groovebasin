comptime {
    _ = @import("callback.zig");
}
const websocket = @import("websocket.zig");
const browser = @import("browser.zig");

export fn main() void {
    browser.print("hello world");
    browser.serveWebSocket(
        &websocket.onOpen,
        undefined,
        &websocket.onClose,
        undefined,
    );
}
