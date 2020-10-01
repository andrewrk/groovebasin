// this is the {env} passed into the wasm instantiation.
// use @import("browser.zig") instead of this.

pub extern fn print(ptr: [*]const u8, len: usize) void;
pub extern fn serveWebSocket(
    openCallbackId: i32,
    openCallbackContext: *@Type(.Opaque),
    closeCallbackId: i32,
    closeCallbackContext: *@Type(.Opaque),
) void;
