const std = @import("std");

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
        &onOpenCallback,
        undefined,
        &onCloseCallback,
        undefined,
        &onErrorCallback,
        undefined,
        &onMessageCallback,
        undefined,
    );
}

fn onOpenCallback(context: *callback.Context, handle: i32) void {
    _ = context;
    _ = async onOpen(handle);
}
fn onOpen(handle: i32) void {
    browser.print("zig: websocket opened");

    std.debug.assert(loading_state == .connecting);
    loading_state = .connected;
    websocket_handle = handle;
    ui.setLoadingState(.good);

    periodic_ping_handle = env.setInterval(&periodicPingCallback, undefined, periodic_ping_interval_ms);
    periodicPingAndCatch();
}

var next_seq_id: u32 = 0;
fn generateSeqId() u32 {
    defer {
        next_seq_id +%= 1;
    }
    return next_seq_id;
}

const ResponseHandler = struct {
    frame: anyframe,
    response_buf_ptr: *[]u8,
};
var pending_requests: std.AutoHashMapUnmanaged(u32, ResponseHandler) = .{};

const Call = struct {
    seq_id: u32,
    request: std.ArrayList(u8),
    response_buf: []u8,
    response: std.io.FixedBufferStream([]u8),
    pub fn init(opcode: protocol.Opcode) !@This() {
        var self = @This(){
            .seq_id = generateSeqId(),
            .request = std.ArrayList(u8).init(gpa),
            .response_buf = undefined,
            .response = undefined,
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

    pub fn send(self: *@This()) !void {
        const buffer = self.request.items;
        suspend {
            try pending_requests.put(gpa, self.seq_id, .{
                .frame = @frame(),
                .response_buf_ptr = &self.response_buf,
            });
            env.sendMessage(websocket_handle, buffer.ptr, buffer.len);
        }
        // we now have a response buf.
        self.response = std.io.fixedBufferStream(self.response_buf);
    }
};

fn onCloseCallback(context: *callback.Context, code: i32) void {
    _ = context;
    _ = async onClose(code);
}
fn onClose(code: i32) void {
    _ = code;
    browser.print("zig: websocket closed");
    handleNoConnection();
}

fn onErrorCallback(context: *callback.Context) void {
    _ = context;
    _ = async onError();
}
fn onError() void {
    browser.print("zig: websocket error");
    handleNoConnection();
}

var gpa_state: std.heap.GeneralPurposeAllocator(.{}) = .{};
const gpa = &gpa_state.allocator;

fn onMessageCallback(context: *callback.Context, handle: i32, _len: i32) void {
    _ = context;
    _ = async onMessage(handle, _len);
}
fn onMessage(handle: i32, _len: i32) void {
    const len = @intCast(usize, _len);

    const buffer = gpa.alloc(u8, len) catch |err| {
        @panic(@errorName(err));
    };
    browser.readBlob(handle, buffer);

    browser.print(buffer);

    var stream = std.io.fixedBufferStream(buffer);
    const reader = stream.reader();
    const header = reader.readStruct(protocol.ResponseHeader) catch |err| {
        @panic(@errorName(err));
    };
    const handler = (pending_requests.fetchRemove(header.seq_id) orelse {
        @panic("received a response for unrecognized seq_id");
    }).value;

    handler.response_buf_ptr.* = buffer;
    resume handler.frame;
}

const retry_timeout_ms = 1000;
fn handleNoConnection() void {
    if (loading_state == .backoff) return;
    loading_state = .backoff;
    ui.setLoadingState(.no_connection);

    env.clearTimer(periodic_ping_handle.?);
    _ = env.setTimeout(&retryOpenCallback, undefined, retry_timeout_ms);
}

fn retryOpenCallback(context: *callback.Context) void {
    _ = context;
    _ = async retryOpen();
}
fn retryOpen() void {
    if (loading_state != .backoff) return;
    loading_state = .none;
    open();
}

var periodic_ping_handle: ?i64 = null;
const periodic_ping_interval_ms = 10_000;
fn periodicPingCallback(context: *callback.Context) void {
    _ = context;
    _ = async periodicPingAndCatch();
}
fn periodicPingAndCatch() void {
    periodicPing() catch |err| {
        @panic(@errorName(err));
    };
}
fn periodicPing() !void {
    {
        var ping_call = try Call.init(.ping);
        try ping_call.send();

        const milliseconds = env.getTime();
        const client_ns = @as(i128, milliseconds) * 1_000_000;
        const server_ns = try ping_call.reader().readIntLittle(i128);
        const lag_ns = client_ns - server_ns;
        ui.setLag(lag_ns);
    }

    // Also by the way, let's query for the data or something.
    {
        var query_call = try Call.init(.query);
        try query_call.writer().writeStruct(protocol.QueryRequest{
            .last_library = 0,
        });
        try query_call.send();

        // TODO: read the response.
    }
}
