const std = @import("std");
const json = std.json;

const browser = @import("browser.zig");
const env = @import("browser_env.zig");
const callback = @import("callback.zig");
const ui = @import("groovebasin_ui.zig");

const protocol = @import("shared").protocol;

var websocket_handle: i32 = undefined;

const LoadingState = enum {
    none,
    connecting,
    connected,
    backoff,
};
var loading_state: LoadingState = .none;

pub fn open() void {
    std.debug.assert(loading_state == .none);
    loading_state = .connecting;
    env.openWebSocket(
        &onOpen,
        undefined,
        &onClose,
        undefined,
        &onError,
        undefined,
        &onMessage,
        undefined,
    );
}

fn onOpen(context: *callback.Context, handle: i32) void {
    _ = context;
    browser.print("zig: websocket opened");

    std.debug.assert(loading_state == .connecting);
    loading_state = .connected;
    websocket_handle = handle;
    ui.setLoadingState(.good);

    // try it out!
    const request = protocol.Request{
        .id = 123,
    };
    writeRequest(request) catch {
        @panic("got an error");
    };
}

fn writeRequest(request: protocol.Request) !void {
    var out_buffer: [0x1000]u8 = undefined;
    var fixed_buffer_stream = std.io.fixedBufferStream(&out_buffer);
    const out_stream = fixed_buffer_stream.writer();
    try json.stringify(request, json.StringifyOptions{}, out_stream);

    sendMessage(fixed_buffer_stream.getWritten());
}

fn onClose(context: *callback.Context, code: i32) void {
    _ = context;
    _ = code;
    browser.print("zig: websocket closed");
    handleNoConnection();
}

fn onError(context: *callback.Context) void {
    _ = context;
    browser.print("zig: websocket error");
    handleNoConnection();
}

fn onMessage(context: *callback.Context, handle: i32, _len: i32) void {
    _ = context;
    const len = @intCast(usize, _len);

    var buffer: [0x1000]u8 = undefined;
    browser.readBlob(handle, buffer[0..len]);

    browser.print(buffer[0..len]);
}

pub fn sendMessage(message: []const u8) void {
    env.sendMessage(websocket_handle, message.ptr, message.len);
}

const retry_timeout_ms = 1000;
fn handleNoConnection() void {
    if (loading_state == .backoff) return;
    loading_state = .backoff;
    ui.setLoadingState(.no_connection);

    env.setTimeout(&retryOpen, undefined, retry_timeout_ms);
}

fn retryOpen(context: *callback.Context) void {
    _ = context;
    if (loading_state != .backoff) return;
    loading_state = .none;
    open();
}
