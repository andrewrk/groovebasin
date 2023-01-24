// A convenient abstraction for essential functions.
const std = @import("std");

const env = @import("browser_env.zig");
const callback = @import("callback.zig");

pub fn print(str: []const u8) void {
    env.print(str.ptr, str.len);
}

pub fn setTimeout(
    cb: callback.Callback,
    milliseconds: i32,
) i64 {
    return env.setTimeout(cb.handle, milliseconds);
}
pub fn setInterval(
    cb: callback.Callback,
    milliseconds: i32,
) i64 {
    return env.setInterval(cb.handle, milliseconds);
}

pub fn openWebSocket(
    allocatorCallback: callback.CallbackI32RI32,
    openCallback: callback.CallbackI32,
    closeCallback: callback.CallbackI32,
    errorCallback: callback.Callback,
    messageCallback: callback.CallbackSliceU8,
) void {
    return env.openWebSocket(
        allocatorCallback.handle,
        openCallback.handle,
        closeCallback.handle,
        errorCallback.handle,
        messageCallback.handle,
    );
}

pub fn setAudioSrc(handle: i32, src: []const u8) void {
    env.setAudioSrc(handle, src.ptr, src.len);
}

pub fn unpackSlice(packed_slice: i64) []u8 {
    const ptr = @intToPtr([*]u8, @bitCast(usize, @intCast(i32, packed_slice >> 32)));
    const len = @bitCast(usize, @intCast(i32, packed_slice & 0xffff_ffff));
    return ptr[0..len];
}
