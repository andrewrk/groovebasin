pub const Context = @Type(.Opaque);

export fn delegateCallback(callback_id: i32, context: *Context) void {
    switch (callback_id) {
        @import("websocket.zig").onOpen_id => @import("websocket.zig").onOpen(context),
        else => unreachable,
    }
}

export fn delegateCallbackI32(callback_id: i32, context: *Context, arg: i32) void {
    switch (callback_id) {
        @import("websocket.zig").onClose_id => @import("websocket.zig").onClose(context, arg),
        else => unreachable,
    }
}
