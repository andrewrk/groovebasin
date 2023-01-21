// This is the mechanism for JS calling Zig function pointers.

const std = @import("std");
const Allocator = std.mem.Allocator;

// FIXME: @distinct(i64) with https://github.com/ziglang/zig/issues/1595
pub const Callback = struct { handle: i64 };
pub const CallbackI32 = struct { handle: i64 };
pub const CallbackI32RI32 = struct { handle: i64 };
pub const CallbackSliceU8 = struct { handle: i64 };

fn isAbiTypeVoid(comptime T: type) bool {
    return switch (@typeInfo(T)) {
        .Void => true,
        .Int, .Pointer => false,
        else => unreachable, // unsupported context type
    };
}
fn TypeForCallback(comptime callback: anytype, comptime ContextType: type) type {
    const callback_fn = @typeInfo(@TypeOf(callback)).Fn;
    const return_error_union = @typeInfo(callback_fn.return_type.?).ErrorUnion;
    std.debug.assert(return_error_union.error_set == anyerror);
    const params = if (isAbiTypeVoid(ContextType)) callback_fn.params else blk: {
        std.debug.assert(callback_fn.params[0].type.? == ContextType);
        break :blk callback_fn.params[1..];
    };
    switch (params.len) {
        0 => {
            std.debug.assert(return_error_union.payload == void);
            return Callback;
        },
        1 => {
            if (params[0].type == []u8 or params[0].type == []const u8) {
                return CallbackSliceU8;
            }
            std.debug.assert(@sizeOf(params[0].type.?) == 4);
            if (isAbiTypeVoid(return_error_union.payload)) {
                return CallbackI32;
            } else {
                std.debug.assert(@sizeOf(return_error_union.payload) == 4);
                return CallbackI32RI32;
            }
        },
        else => unreachable,
    }
}

pub fn packCallback(comptime callback: anytype, context: anytype) TypeForCallback(callback, @TypeOf(context)) {
    const context_int: i32 = switch (@typeInfo(@TypeOf(context))) {
        .Void => 0,
        .Int => @bitCast(i32, context),
        .Pointer => @bitCast(i32, @ptrToInt(context)),
        else => unreachable,
    };

    return TypeForCallback(callback, @TypeOf(context)){
        .handle = @bitCast(i64, DecodedCallback{
            .callback = @intCast(u31, @ptrToInt(&callback)),
            .context_int = context_int,
            .is_void = isAbiTypeVoid(@TypeOf(context)),
        }),
    };
}

const DecodedCallback = packed struct {
    callback: u31,
    is_void: bool,
    context_int: i32,
    comptime {
        std.debug.assert(@sizeOf(DecodedCallback) == 8);
    }
};

pub export fn delegateCallback(packed_callback: i64) void {
    const unpacked = @bitCast(DecodedCallback, packed_callback);
    if (unpacked.is_void) {
        @intToPtr(*const fn () anyerror!void, unpacked.callback)() catch |err| {
            @panic(@errorName(err));
        };
    } else {
        @intToPtr(*const fn (i32) anyerror!void, unpacked.callback)(unpacked.context_int) catch |err| {
            @panic(@errorName(err));
        };
    }
}

pub export fn delegateCallbackI32(packed_callback: i64, arg: i32) void {
    const unpacked = @bitCast(DecodedCallback, packed_callback);
    if (unpacked.is_void) {
        @intToPtr(*const fn (i32) anyerror!void, unpacked.callback)(arg) catch |err| {
            @panic(@errorName(err));
        };
    } else {
        @intToPtr(*const fn (i32, i32) anyerror!void, unpacked.callback)(unpacked.context_int, arg) catch |err| {
            @panic(@errorName(err));
        };
    }
}

pub export fn delegateCallbackSliceU8(packed_callback: i64, ptr: [*]u8, len: usize) void {
    const unpacked = @bitCast(DecodedCallback, packed_callback);
    if (unpacked.is_void) {
        @intToPtr(*const fn ([]u8) anyerror!void, unpacked.callback)(ptr[0..len]) catch |err| {
            @panic(@errorName(err));
        };
    } else {
        @intToPtr(*const fn (i32, []u8) anyerror!void, unpacked.callback)(unpacked.context_int, ptr[0..len]) catch |err| {
            @panic(@errorName(err));
        };
    }
}

pub export fn delegateCallbackI32RI32(packed_callback: i64, arg: i32) i32 {
    const unpacked = @bitCast(DecodedCallback, packed_callback);
    if (unpacked.is_void) {
        return @intToPtr(*const fn (i32) anyerror!i32, unpacked.callback)(arg) catch |err| {
            @panic(@errorName(err));
        };
    } else {
        return @intToPtr(*const fn (i32, i32) anyerror!i32, unpacked.callback)(unpacked.context_int, arg) catch |err| {
            @panic(@errorName(err));
        };
    }
}

/// Exposes an allocator to JS.
pub fn allocator(a: *Allocator) CallbackI32RI32 {
    return packCallback(allocatorCallback, a);
}
fn allocatorCallback(a: *Allocator, len: usize) anyerror!i32 {
    const slice = try a.alloc(u8, len);
    return @bitCast(i32, @ptrToInt(slice.ptr));
}
