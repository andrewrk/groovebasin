const std = @import("std");
const websocket_handler = @import("websocket_handler.zig");
const browser = @import("browser.zig");
const env = @import("browser_env.zig");
const ui = @import("groovebasin_ui.zig");
const stream = @import("stream.zig");
const g = @import("global.zig");

fn log(
    comptime level: std.log.Level,
    comptime scope: @TypeOf(.EnumLiteral),
    comptime format: []const u8,
    args: anytype,
) void {
    const msg = std.fmt.allocPrint(
        g.gpa,
        "[" ++ level.asText() ++ "] (" ++ @tagName(scope) ++ "): " ++ format,
        args,
    ) catch |err| {
        browser.print(@errorName(err));
        return;
    };
    defer g.gpa.free(msg);

    browser.print(msg);
}

pub const std_options = struct {
    pub const logFn = log;
};

export fn main() void {
    std.log.info("hello world: {s}", .{"zig"});
    ui.init();
    stream.init();

    websocket_handler.open();
}

pub fn panic(msg: []const u8, stacktrace: ?*std.builtin.StackTrace, ra: ?usize) noreturn {
    _ = ra;
    _ = stacktrace;
    env.panic(msg.ptr, msg.len);
    unreachable;
}
