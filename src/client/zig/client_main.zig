comptime {
    _ = @import("callback.zig");
}
const websocket_handler = @import("websocket_handler.zig");
const browser = @import("browser.zig");

export fn main() void {
    browser.print("zig: hello world");
    if (false) {
        browser.serveWebSocket(
            &websocket_handler.onOpen,
            undefined,
            &websocket_handler.onClose,
            undefined,
            &websocket_handler.onError,
            undefined,
            &websocket_handler.onMessage,
            undefined,
        );
    }
}
