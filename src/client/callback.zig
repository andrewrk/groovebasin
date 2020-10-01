pub const Context = @Type(.Opaque);
pub const CallbackFn = fn(context: *Context) void;
pub const CallbackFnI32 = fn(context: *Context, arg: i32) void;

export fn delegateCallback(callback: *const CallbackFn, context: *Context) void {
    callback.*(context);
}

export fn delegateCallbackI32(callback: *const CallbackFnI32, context: *Context, arg: i32) void {
    callback.*(context, arg);
}
