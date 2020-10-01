// this is the {env} passed into the wasm instantiation.
// use @import("browser.zig") instead of this.

const callback = @import("callback.zig");

pub extern fn print(ptr: [*]const u8, len: usize) void;
pub extern fn serveWebSocket(
    openCallback: *const callback.CallbackFn,
    openCallbackContext: *callback.Context,
    closeCallbackId: *const callback.CallbackFnI32,
    closeCallbackContext: *callback.Context,
) void;
