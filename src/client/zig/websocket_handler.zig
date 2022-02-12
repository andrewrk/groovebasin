const std = @import("std");
const assert = std.debug.assert;

const browser = @import("browser.zig");
const env = @import("browser_env.zig");
const callback = @import("callback.zig");
const ui = @import("groovebasin_ui.zig");
const g = @import("global.zig");

const protocol = @import("shared").protocol;
const log = std.log.scoped(.websocket);

var websocket_handle: i32 = undefined;

const LoadingState = enum {
    none,
    connecting,
    connected,
    backoff,
};
var loading_state: LoadingState = .none;

pub fn open() void {
    assert(loading_state == .none);
    loading_state = .connecting;

    browser.openWebSocket(
        callback.allocator(&g.gpa),
        callback.packCallback(onOpenCallback, {}),
        callback.packCallback(onCloseCallback, {}),
        callback.packCallback(onErrorCallback, {}),
        callback.packCallback(onMessageCallback, {}),
    );
}

fn onOpenCallback(handle: i32) anyerror!void {
    log.info("zig: websocket opened", .{});

    assert(loading_state == .connecting);
    loading_state = .connected;
    websocket_handle = handle;
    ui.setLoadingState(.good);

    periodic_ping_handle = browser.setInterval(callback.packCallback(periodicPing, {}), periodic_ping_interval_ms);
    try periodicPing();
}

var next_seq_id: u32 = 0;
fn generateSeqId() u32 {
    defer {
        next_seq_id += 1;
        next_seq_id &= 0x7fff_ffff;
    }
    return next_seq_id;
}

var pending_requests: std.AutoHashMapUnmanaged(u32, callback.CallbackSliceU8) = .{};

pub const Call = struct {
    seq_id: u32,
    request: std.ArrayList(u8),
    pub fn init(opcode: protocol.Opcode) !@This() {
        var self = @This(){
            .seq_id = generateSeqId(),
            .request = std.ArrayList(u8).init(g.gpa),
        };

        // write the request header.
        try self.request.writer().writeStruct(protocol.RequestHeader{
            .seq_id = self.seq_id,
            .op = opcode,
        });

        return self;
    }

    pub fn deinit(self: *@This()) void {
        self.request.deinit();
        self.response.deinit();
    }

    pub fn writer(self: *@This()) std.ArrayList(u8).Writer {
        return self.request.writer();
    }

    pub fn reader(self: *@This()) std.io.FixedBufferStream([]u8).Reader {
        return self.response.reader();
    }

    pub fn send(self: *@This(), comptime cb: anytype, context: anytype) !void {
        const buffer = self.request.items;
        try pending_requests.put(g.gpa, self.seq_id, callback.packCallback(cb, context));
        log.info("request: {}", .{std.fmt.fmtSliceHexLower(buffer)});
        env.sendMessage(websocket_handle, buffer.ptr, buffer.len);
    }
};

pub fn ignoreResponse(response: []const u8) anyerror!void {
    _ = response;
}

fn onCloseCallback(code: i32) anyerror!void {
    _ = code;
    log.info("zig: websocket closed", .{});
    handleNoConnection();
}

fn onErrorCallback() anyerror!void {
    log.info("zig: websocket error", .{});
    handleNoConnection();
}

fn onMessageCallback(buffer: []u8) anyerror!void {
    defer g.gpa.free(buffer);

    log.info("response: {}", .{std.fmt.fmtSliceHexLower(buffer)});

    var stream = std.io.fixedBufferStream(buffer);
    const reader = stream.reader();
    const header = try reader.readStruct(protocol.ResponseHeader);
    const remaining_buffer = buffer[stream.pos..];

    if ((header.seq_id & 0x8000_0000) == 0) {
        // response to a request.

        const cb = (pending_requests.fetchRemove(header.seq_id) orelse {
            @panic("received a response for unrecognized seq_id");
        }).value;
        callback.delegateCallbackSliceU8(cb.handle, remaining_buffer.ptr, remaining_buffer.len);
    } else {
        // message from the server.
        try handlePushMessage(remaining_buffer);
    }
}

const retry_timeout_ms = 1000;
fn handleNoConnection() void {
    if (loading_state == .backoff) return;
    loading_state = .backoff;
    ui.setLoadingState(.no_connection);

    env.clearTimer(periodic_ping_handle.?);
    _ = browser.setTimeout(callback.packCallback(retryOpenCallback, {}), retry_timeout_ms);
}

fn retryOpenCallback() anyerror!void {
    if (loading_state != .backoff) return;
    loading_state = .none;
    open();
}

var periodic_ping_handle: ?i64 = null;
const periodic_ping_interval_ms = 10_000;
fn periodicPing() anyerror!void {
    {
        var ping_call = try Call.init(.ping);
        try ping_call.send(handlePeriodicPingResponse, {});
    }

    // Also by the way, let's query for the data or something.
    try ui.poll();
}

fn handlePeriodicPingResponse(response: []const u8) anyerror!void {
    var stream = std.io.fixedBufferStream(response);
    const milliseconds = env.getTime();
    const client_ns = @as(i128, milliseconds) * 1_000_000;
    const server_ns = try stream.reader().readIntLittle(i128);
    const lag_ns = client_ns - server_ns;
    ui.setLag(lag_ns);
}

fn handlePushMessage(response: []const u8) !void {
    assert(response.len == 0);
    try ui.poll();
}
