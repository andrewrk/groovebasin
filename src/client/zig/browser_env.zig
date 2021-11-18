// This is the {env} passed into the wasm instantiation.
// Use @import("browser.zig") instead of this.

const callback = @import("callback.zig");

// Essentials
pub extern fn print(ptr: [*]const u8, len: usize) void;
pub extern fn panic(ptr: [*]const u8, len: usize) void;
pub extern fn readBlob(handle: i32, ptr: [*]u8, len: usize) void;
pub extern fn getTime() i64;
pub extern fn setTimeout(
    callback: *const callback.CallbackFn,
    callbackContext: *callback.Context,
    milliseconds: i32,
) i64;
pub extern fn setInterval(
    callback: *const callback.CallbackFn,
    callbackContext: *callback.Context,
    milliseconds: i32,
) i64;
pub extern fn clearTimer(handle: i64) void;

// WebSocket API
pub extern fn openWebSocket(
    openCallback: *const callback.CallbackFnI32,
    openCallbackContext: *callback.Context,
    closeCallbackId: *const callback.CallbackFnI32,
    closeCallbackContext: *callback.Context,
    errorCallback: *const callback.CallbackFn,
    errorCallbackContext: *callback.Context,
    messageCallbackId: *const callback.CallbackFnI32I32,
    messageCallbackContext: *callback.Context,
) void;
pub extern fn sendMessage(handle: i32, ptr: [*]const u8, len: usize) void;

// Dom
pub extern fn getElementById(ptr: [*]const u8, len: usize) i32;
pub extern fn releaseElementHandle(handle: i32) void;
pub extern fn setElementShown(handle: i32, shown: i32) void;
pub extern fn setElementTextContent(handle: i32, ptr: [*]const u8, len: usize) void;
