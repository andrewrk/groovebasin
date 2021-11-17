comptime {
    _ = @import("callback.zig");
}
const websocket_handler = @import("websocket_handler.zig");
const browser = @import("browser.zig");
const ui = @import("groovebasin_ui.zig");

export fn main() void {
    browser.print("zig: hello world");
    ui.loadDomElements();

    browser.openWebSocket(
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
