// This is the {env} passed into the wasm instantiation.
// Use @import("browser.zig") instead of this.

const callback = @import("callback.zig");

pub extern fn print(ptr: [*]const u8, len: usize) void;
pub extern fn serveWebSocket(
    openCallback: *const callback.CallbackFn,
    openCallbackContext: *callback.Context,
    closeCallbackId: *const callback.CallbackFnI32,
    closeCallbackContext: *callback.Context,
    errorCallback: *const callback.CallbackFn,
    errorCallbackContext: *callback.Context,
    messageCallbackId: *const callback.CallbackFnI32I32,
    messageCallbackContext: *callback.Context,
) void;

pub extern fn readBlob(handle: i32, ptr: [*]u8, len: usize) void;
