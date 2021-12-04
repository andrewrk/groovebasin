// This is the mechanism for JS calling Zig function pointers.

const std = @import("std");
const Allocator = std.mem.Allocator;

pub const Context = *allowzero opaque {};

pub const CallbackFn = fn (Context) void;
export fn delegateCallback(callback: *const CallbackFn, context: Context) void {
    callback.*(context);
}

pub const CallbackFnI32 = fn (Context, i32) void;
export fn delegateCallbackI32(callback: *const CallbackFnI32, context: Context, arg: i32) void {
    callback.*(context, arg);
}

pub const CallbackFnSliceU8 = fn (Context, []u8) void;
export fn delegateCallbackSliceU8(callback: *const CallbackFnSliceU8, context: Context, ptr: [*]u8, len: usize) void {
    callback.*(context, ptr[0..len]);
}

pub const CallbackFnI32RI32 = fn (Context, i32) i32;
export fn delegateCallbackI32RI32(callback: *const CallbackFnI32RI32, context: Context, arg: i32) i32 {
    return callback.*(context, arg);
}

/// Convenience function that does all the type casts for exposing an allocator to JS.
pub fn allocator(a: *Allocator) AllocatorCallback {
    return AllocatorCallback{
        .callback = &allocatorCallback,
        .context = @ptrCast(Context, a),
    };
}
const AllocatorCallback = struct {
    callback: *const CallbackFnI32RI32,
    context: Context,
};
fn allocatorCallback(context: Context, len_: i32) i32 {
    const a = @ptrCast(*Allocator, @alignCast(4, context));
    const len = @bitCast(usize, len_);
    const slice = a.alloc(u8, len) catch |err| {
        @panic(@errorName(err));
    };
    return @bitCast(i32, @ptrToInt(slice.ptr));
}
