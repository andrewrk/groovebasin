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
    const args = if (isAbiTypeVoid(ContextType)) callback_fn.args else blk: {
        std.debug.assert(callback_fn.args[0].arg_type.? == ContextType);
        break :blk callback_fn.args[1..];
    };
    switch (args.len) {
        0 => {
            std.debug.assert(return_error_union.payload == void);
            return Callback;
        },
        1 => {
            if (args[0].arg_type == []u8 or args[0].arg_type == []const u8) {
                return CallbackSliceU8;
            }
            std.debug.assert(@sizeOf(args[0].arg_type.?) == 4);
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
    comptime std.debug.assert(@alignOf(@TypeOf(&callback)) == 4);
    const callback_int = @ptrToInt(&callback) | comptime if (isAbiTypeVoid(@TypeOf(context))) 0 else 1;
    const context_int: u32 = switch (@typeInfo(@TypeOf(context))) {
        .Void => 0,
        .Int => @bitCast(u32, context),
        .Pointer => @bitCast(u32, @ptrToInt(context)),
        else => unreachable,
    };

    return TypeForCallback(callback, @TypeOf(context)){
        .handle = @bitCast(i64, (@as(u64, context_int) << 32) | @as(u64, callback_int)),
    };
}

const SomeCallbackFn = *align(4) opaque {};
const UnpackedCallback = struct {
    context_int: i32,
    callback: SomeCallbackFn,
    is_void: bool,
};
inline fn unpackCallback(packed_callback: i64) UnpackedCallback {
    return UnpackedCallback{
        .context_int = @intCast(i32, packed_callback >> 32),
        .callback = @intToPtr(SomeCallbackFn, @intCast(usize, packed_callback & 0xffff_fffc)),
        .is_void = switch (packed_callback & 0b11) {
            0 => true,
            1 => false,
            else => unreachable,
        },
    };
}

pub export fn delegateCallback(packed_callback: i64) void {
    const unpacked = unpackCallback(packed_callback);
    if (unpacked.is_void) {
        @ptrCast(*const fn () anyerror!void, unpacked.callback)() catch |err| {
            @panic(@errorName(err));
        };
    } else {
        @ptrCast(*const fn (i32) anyerror!void, unpacked.callback)(unpacked.context_int) catch |err| {
            @panic(@errorName(err));
        };
    }
}

pub export fn delegateCallbackI32(packed_callback: i64, arg: i32) void {
    const unpacked = unpackCallback(packed_callback);
    if (unpacked.is_void) {
        @ptrCast(*const fn (i32) anyerror!void, unpacked.callback)(arg) catch |err| {
            @panic(@errorName(err));
        };
    } else {
        @ptrCast(*const fn (i32, i32) anyerror!void, unpacked.callback)(unpacked.context_int, arg) catch |err| {
            @panic(@errorName(err));
        };
    }
}

pub export fn delegateCallbackSliceU8(packed_callback: i64, ptr: [*]u8, len: usize) void {
    const unpacked = unpackCallback(packed_callback);
    if (unpacked.is_void) {
        @ptrCast(*const fn ([]u8) anyerror!void, unpacked.callback)(ptr[0..len]) catch |err| {
            @panic(@errorName(err));
        };
    } else {
        @ptrCast(*const fn (i32, []u8) anyerror!void, unpacked.callback)(unpacked.context_int, ptr[0..len]) catch |err| {
            @panic(@errorName(err));
        };
    }
}

pub export fn delegateCallbackI32RI32(packed_callback: i64, arg: i32) i32 {
    const unpacked = unpackCallback(packed_callback);
    if (unpacked.is_void) {
        return @ptrCast(*const fn (i32) anyerror!i32, unpacked.callback)(arg) catch |err| {
            @panic(@errorName(err));
        };
    } else {
        return @ptrCast(*const fn (i32, i32) anyerror!i32, unpacked.callback)(unpacked.context_int, arg) catch |err| {
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
