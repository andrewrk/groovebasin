// This is the mechanism for JS calling Zig function pointers.

pub const Context = opaque {};

pub const CallbackFn = fn (*Context) void;
export fn delegateCallback(callback: *const CallbackFn, context: *Context) void {
    callback.*(context);
}

pub const CallbackFnI32 = fn (*Context, i32) void;
export fn delegateCallbackI32(callback: *const CallbackFnI32, context: *Context, arg: i32) void {
    callback.*(context, arg);
}

pub const CallbackFnI32I32 = fn (*Context, i32, i32) void;
export fn delegateCallbackI32I32(callback: *const CallbackFnI32I32, context: *Context, arg1: i32, arg2: i32) void {
    callback.*(context, arg1, arg2);
}
