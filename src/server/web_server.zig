const std = @import("std");
const mem = std.mem;
const net = std.net;
const log = std.log.scoped(.net);
const groove_log = std.log.scoped(.groove);
const Channel = @import("threadsafe_queue.zig").Channel;
const channel = @import("threadsafe_queue.zig").channel;
const RefCounter = @import("RefCounter.zig");
const LinearFifo = std.fifo.LinearFifo;
const Groove = @import("groove.zig").Groove;
const StaticHttpFileServer = @import("StaticHttpFileServer");

const groovebasin_protocol = @import("groovebasin_protocol.zig");
const Id = groovebasin_protocol.Id;
const server_logic = @import("server_main.zig");

const g = @import("global.zig");

// threads:
//  1. main thread (server logic)
//  2. listen thread
//  2n+0. client[n] recv thread
//  2n+1. client[n] send thread
// all threads other than main thread are contained in this file.
//
// cross-thread memory management:
//  each incoming tcp connection:
//   listen thread: create refcounted handler object backed by gpa. increment to 2.
//   the send thread: on exit, decrement ref, possibly freeing from gpa.
//   the recv thread: on exit, decrement ref, possibly freeing from gpa.
//  each incoming websocket message:
//   recv thread: json.parse'd into a special arena backed by gpa.
//   main thread: continues using special arena.
//   main thread: deinit special arena.
//  each sent message:
//   TODO: document this one.
//
// shutdown routine:
//  uhhhh Ctrl+C lmao.

// Server state
var to_server_channel = channel(LinearFifo(*ToServerMessage, .{ .Static = 64 }).init());

const ToServerMessage = struct {
    refcounter: RefCounter = .{},
    event: union(enum) {
        client_to_server_message: struct {
            client_id: Id,
            message: *RefCountedByteSlice,
        },
        client_connected: Id,
        client_disconnected: Id,
    },

    pub fn ref(self: *@This()) void {
        self.refcounter.ref();
        switch (self.event) {
            .client_to_server_message => |m| {
                m.message.ref(); // trace:ToServerMessage
            },
            else => {},
        }
    }
    pub fn unref(self: *@This()) void {
        if (self.refcounter.unref()) {
            switch (self.event) {
                .client_to_server_message => |m| {
                    m.message.unref(); // trace:ToServerMessage
                },
                else => {},
            }
            g.gpa.destroy(self);
        }
    }
};

pub fn listen(addr: net.Address, static_http_file_server: *StaticHttpFileServer) !void {
    const gpa = g.gpa;

    log.debug("init tcp server", .{});
    var server = try addr.listen(.{
        .reuse_address = true,
    });
    defer server.deinit();
    log.info("listening at {}", .{server.listen_address});

    while (true) {
        const connection = try server.accept();
        const handler = try gpa.create(ConnectionHandler);
        handler.* = .{
            .connection = connection,
            .id = Id.random(),
            .static_http_file_server = static_http_file_server,
        };
        handler.ref(); // trace:ConnectionHandler.entrypoint
        {
            handlers_mutex.lock();
            defer handlers_mutex.unlock();
            try handlers.putNoClobber(gpa, handler.id, handler);
        }

        _ = std.Thread.spawn(.{}, ConnectionHandler.entrypoint, .{handler}) catch |err| {
            log.err("can't spawn connection handler thread: {s}", .{@errorName(err)});
            {
                handlers_mutex.lock();
                defer handlers_mutex.unlock();
                std.debug.assert(handlers.swapRemove(handler.id));
            }
            handler.unref(); // trace:ConnectionHandler.entrypoint
            continue;
        };
    }
}

const WebsocketSendFifo = LinearFifo(?*RefCountedByteSlice, .{ .Static = 16 });

const ConnectionHandler = struct {
    connection: net.Server.Connection,
    id: Id,
    refcounter: RefCounter = .{},

    // only used for websocket handlers
    websocket_send_queue: Channel(WebsocketSendFifo) = channel(WebsocketSendFifo.init()),
    is_closing: std.atomic.Value(bool) = .{ .raw = false },

    static_http_file_server: *StaticHttpFileServer,

    pub fn ref(self: *@This()) void {
        self.refcounter.ref();
    }
    pub fn unref(self: *@This()) void {
        if (self.refcounter.unref()) {
            log.debug("destroying connection handler", .{});
            self.close();
            g.gpa.destroy(self);
        }
    }
    pub fn close(self: *@This()) void {
        if (self.is_closing.swap(true, .SeqCst) == true) return;
        // We're the thread that does the shutdown.
        self.connection.stream.close();
        // Drain and unref everything in the send queue.
        while (self.websocket_send_queue.get()) |item| {
            item.?.unref(); // trace:websocket_send_queue
        }
        // Wake up the send thread if necessary.
        self.websocket_send_queue.put(null) catch unreachable;
    }

    fn entrypoint(self: *@This()) void {
        self.handleConnection() catch |err| switch (err) {
            error.UnsupportedRequest => {
                log.warn("unsupported HTTP request", .{});
            },
            error.ConnectionResetByPeer, error.ConnectionTimedOut => {
                log.warn("client socket error: {s}", .{@errorName(err)});
            },
            error.BrokenPipe => {
                log.debug("client handling complete (broken pipe)", .{});
            },
            else => log.err("error while handling connection: {s}", .{@errorName(err)}),
        };
        {
            handlers_mutex.lock();
            defer handlers_mutex.unlock();
            std.debug.assert(handlers.swapRemove(self.id));
        }
        self.unref(); // trace:ConnectionHandler.entrypoint
    }

    fn handleConnection(self: *@This()) !void {
        var read_buffer: [0x4000]u8 = undefined;
        var http_server = std.http.Server.init(self.connection, &read_buffer);

        while (http_server.state == .ready) {
            var request = try http_server.receiveHead();
            try self.handleRequest(&request);
        }
    }

    fn handleRequest(self: *@This(), req: *std.http.Server.Request) !void {
        if (mem.eql(u8, req.head.target, "/stream.mp3")) {
            // This is going to stay open for a long time.
            return self.streamEndpoint(req);
        }

        // Find interesting headers.
        var sec_websocket_key: ?[]const u8 = null;
        var should_upgrade_websocket: bool = false;
        var it = req.iterateHeaders();
        while (it.next()) |header| {
            if (std.ascii.eqlIgnoreCase(header.name, "sec-websocket-key")) {
                sec_websocket_key = header.value;
            } else if (std.ascii.eqlIgnoreCase(header.name, "upgrade")) {
                if (!std.mem.eql(u8, header.value, "websocket"))
                    return error.UnsupportedRequest; // unsupported protocol upgrade
                should_upgrade_websocket = true;
            }
        }

        if (should_upgrade_websocket) {
            const websocket_key = sec_websocket_key orelse
                return error.UnsupportedRequest; // websocket upgrade missing Sec-WebSocket-Key
            log.debug("GET websocket: {s}", .{req.head.target});
            // This is going to stay open for a long time.
            return self.serveWebsocket(websocket_key);
        }

        log.debug("GET static: {s}", .{req.head.target});
        try self.static_http_file_server.serve(req);
    }

    fn streamEndpoint(ch: *ConnectionHandler, req: *std.http.Server.Request) !void {
        _ = ch;
        const player = &g.player;

        var send_buffer: [0x4000]u8 = undefined;
        var response = req.respondStreaming(.{
            .send_buffer = &send_buffer,
            .respond_options = .{
                .extra_headers = &.{
                    .{ .name = "content-type", .value = "audio/mpeg" },
                    .{ .name = "cache-control", .value = "no-cache, no-store, must-revalidate" },
                    .{ .name = "pragma", .value = "no-cache" },
                    .{ .name = "expires", .value = "0" },
                },
            },
        });

        const w = response.writer();

        const logging_interval = 5000;
        var last_logged_time = std.time.milliTimestamp() - 2 * logging_interval;
        while (true) {
            var do_logging = false;
            const now = std.time.milliTimestamp();
            if (now - last_logged_time > logging_interval) {
                do_logging = true;
                last_logged_time = now;
            }

            var buffer: ?*Groove.Buffer = null;

            if (do_logging) {
                var seconds: f64 = undefined;
                player.playlist.position(null, &seconds);
                const is_playing = player.playlist.playing();
                groove_log.debug("stream endpoint buffer_get (playlist head: {d}, playing: {})", .{ seconds, is_playing });
            }
            const status = try player.encoder.buffer_get(&buffer, true);
            if (do_logging) {
                groove_log.debug("stream endpoint buffer_get returned {s}", .{if (buffer == null) "null" else "non-null"});
            }
            _ = status;
            if (buffer) |buf| {
                defer buf.unref();
                const data = buf.data[0][0..@intCast(buf.size)];
                if (do_logging) {
                    groove_log.debug("stream endpoint writing {d} bytes", .{data.len});
                }
                try w.writeAll(data);
            }
        }
    }

    fn serveWebsocket(self: *@This(), key: []const u8) !void {
        {
            // See https://tools.ietf.org/html/rfc6455
            var sha1 = std.crypto.hash.Sha1.init(.{});
            sha1.update(key);
            sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
            var digest: [std.crypto.hash.Sha1.digest_length]u8 = undefined;
            sha1.final(&digest);
            var base64_digest: [28]u8 = undefined;
            std.debug.assert(std.base64.standard.Encoder.encode(&base64_digest, &digest).len == base64_digest.len);

            var iovecs = [_]std.os.iovec_const{
                strToIovec(http_response_header_upgrade),
                strToIovec("Sec-WebSocket-Accept: "),
                strToIovec(&base64_digest),
                strToIovec("\r\n" ++ "\r\n"),
            };
            try self.connection.stream.writevAll(&iovecs);
        }

        self.ref(); // trace:websocketSendLoop
        _ = std.Thread.spawn(.{}, @This().websocketSendLoop, .{self}) catch |err| {
            log.err("spawning websocket handler thread failed: {s}", .{@errorName(err)});
            self.unref(); // trace:websocketSendLoop
            return err;
        };
        defer self.close();

        // Welcome to the party.
        {
            const delivery = try g.gpa.create(ToServerMessage);
            delivery.* = .{ .event = .{ .client_connected = self.id } };
            delivery.ref(); // trace:to_server_channel
            to_server_channel.put(delivery) catch |err| {
                log.err("server overloaded!", .{});
                delivery.unref(); // trace:to_server_channel
                return err;
            };
        }
        defer {
            // Goodbye from the party.
            if (g.gpa.create(ToServerMessage)) |delivery| {
                delivery.* = .{ .event = .{ .client_disconnected = self.id } };
                delivery.ref(); // trace:to_server_channel
                to_server_channel.put(delivery) catch {
                    // TODO: this error should be impossible by design somehow.
                    log.err("server overloaded!", .{});
                    delivery.unref(); // trace:to_server_channel
                };
            } else |_| {
                // TODO: this error should be impossible by design somehow.
                log.err("server overloaded!", .{});
            }
        }

        while (true) {
            // Receive message.
            const request = (try self.readMessage()) orelse break;
            request.ref(); // trace:recv_buffer
            defer request.unref(); // trace:recv_buffer
            // TODO: this logs passwords in clear text:
            log.debug("received: {s}", .{request.payload});

            const delivery = try g.gpa.create(ToServerMessage);
            delivery.* = .{ .event = .{ .client_to_server_message = .{
                .client_id = self.id,
                .message = request,
            } } };
            delivery.ref(); // trace:to_server_channel

            // Deliver to server logic thread.
            to_server_channel.put(delivery) catch |err| {
                log.err("server overloaded!", .{});
                delivery.unref(); // trace:to_server_channel
                return err;
            };
        }
    }

    fn readMessage(self: *@This()) !?*RefCountedByteSlice {
        // See https://tools.ietf.org/html/rfc6455
        // read first byte.
        var header = [_]u8{0} ** 2;
        self.connection.stream.reader().readNoEof(header[0..]) catch |err| switch (err) {
            error.EndOfStream => return null,
            else => return err,
        };
        const opcode_byte = header[0];
        // 0b10000000: FIN - this is a complete message.
        // 0b00000001: opcode=1 - this is a text message.
        // 0b00001000: opcode=8 - denotes a connection close.
        const complete_text_message_opcode = 0b10000001;
        const close_message_opcode = 0b10001000;
        switch (opcode_byte) {
            complete_text_message_opcode => {},
            close_message_opcode => {
                log.debug("websocket client requested clean shutdown", .{});
                return null;
            },
            else => {
                log.warn("bad opcode byte: {}", .{opcode_byte});
                return null;
            },
        }

        // read length
        const short_len_byte = header[1];
        if (short_len_byte & 0b10000000 != 0b10000000) {
            log.warn("frames from client must be masked: {}", .{short_len_byte});
            return null;
        }
        const len: u64 = switch (short_len_byte & 0b01111111) {
            127 => blk: {
                var len_buffer = [_]u8{0} ** 8;
                try self.connection.stream.reader().readNoEof(len_buffer[0..]);
                break :blk std.mem.readInt(u64, &len_buffer, .big);
            },
            126 => blk: {
                var len_buffer = [_]u8{0} ** 2;
                try self.connection.stream.reader().readNoEof(len_buffer[0..]);
                break :blk std.mem.readInt(u16, &len_buffer, .big);
            },
            else => |short_len| blk: {
                break :blk short_len;
            },
        };
        if (len > max_payload_size) {
            log.warn("payload too big: {}", .{len});
            return null;
        }

        // read mask
        var mask_buffer = [_]u8{0} ** 4;
        try self.connection.stream.reader().readNoEof(&mask_buffer);
        const mask_native: u32 = @bitCast(mask_buffer);

        // read payload
        const allocated_len = std.mem.alignForward(usize, len, 4);
        const payload_aligned = try g.gpa.allocWithOptions(u8, allocated_len, 4, null);
        const payload = payload_aligned[0..len];
        try self.connection.stream.reader().readNoEof(payload);

        // unmask
        // The last item may contain a partial word of unused data.
        const payload_as_u32_array: []u32 = std.mem.bytesAsSlice(u32, payload_aligned);
        {
            var i: usize = 0;
            while (i < payload_as_u32_array.len) : (i += 1) {
                payload_as_u32_array[i] ^= mask_native;
            }
        }

        const result = try g.gpa.create(RefCountedByteSlice);
        result.* = .{ .payload = payload, .is_align_4 = true };
        return result;
    }

    fn websocketSendLoop(self: *@This()) void {
        defer self.unref(); // trace:websocketSendLoop
        while (true) {
            const delivery = self.websocket_send_queue.getBlocking() orelse break;
            defer delivery.unref(); // trace:websocket_send_queue

            self.writeMessageFromSendThread(delivery.payload) catch |err| {
                log.err("error writing message to websocket: {s}", .{@errorName(err)});
                break;
            };
        }
        self.close();
    }

    fn writeMessageFromSendThread(self: *@This(), message: []const u8) !void {
        //log.debug("sending: {s}", .{message});
        // See https://tools.ietf.org/html/rfc6455
        var header_buf: [2 + 8]u8 = undefined;
        // 0b10000000: FIN - this is a complete message.
        // 0b00000001: opcode=1 - this is a text message.
        header_buf[0] = 0b10000001;
        const header = switch (message.len) {
            0...125 => blk: {
                // small size
                header_buf[1] = @as(u8, @intCast(message.len));
                break :blk header_buf[0..2];
            },
            126...0xffff => blk: {
                // 16-bit size
                header_buf[1] = 126;
                std.mem.writeInt(u16, header_buf[2..4], @as(u16, @intCast(message.len)), .big);
                break :blk header_buf[0..4];
            },
            else => blk: {
                // 64-bit size
                header_buf[1] = 127;
                std.mem.writeInt(u64, header_buf[2..10], message.len, .big);
                break :blk header_buf[0..10];
            },
        };

        var iovecs = [_]std.os.iovec_const{
            strToIovec(header),
            strToIovec(message),
        };
        try self.connection.stream.writevAll(&iovecs);
    }
};

const http_response_header_upgrade = "" ++
    "HTTP/1.1 101 Switching Protocols\r\n" ++
    "Upgrade: websocket\r\n" ++
    "Connection: Upgrade\r\n";

/// Defense against clients running us out of memory.
const max_payload_size = 16 * 1024 * 1024;

fn strToIovec(s: []const u8) std.os.iovec_const {
    return .{
        .iov_base = s.ptr,
        .iov_len = s.len,
    };
}

var handlers: std.AutoArrayHashMapUnmanaged(Id, *ConnectionHandler) = .{};
var handlers_mutex: std.Thread.Mutex = .{};

/// Takes ownership of message_bytes, even when an error is returned.
pub fn sendMessageToClient(client_id: Id, message_bytes: []const u8) !void {
    const handler = blk: {
        // TODO: optimize with an RW lock
        handlers_mutex.lock();
        defer handlers_mutex.unlock();
        break :blk handlers.get(client_id).?;
    };
    const delivery = g.gpa.create(RefCountedByteSlice) catch |err| {
        g.gpa.free(message_bytes);
        return err;
    };
    delivery.* = .{ .payload = message_bytes };
    delivery.ref(); // trace:websocket_send_queue
    if (handler.is_closing.load(.SeqCst)) {
        // TODO: This is unsound. This flag needs to be attached to the queue mutex i think.
        delivery.unref();
        return;
    }
    handler.websocket_send_queue.put(delivery) catch |err| {
        log.err("websocket client send queue backed up. {s}. closing.", .{@errorName(err)});
        handler.close();
        delivery.unref(); // trace:websocket_send_queue
    };
}

pub fn mainLoop() noreturn {
    while (true) {
        serverLogicOneIteration() catch |err| {
            log.err("Error handling message: {s}", .{@errorName(err)});
        };
    }
}

fn serverLogicOneIteration() !void {
    const delivery = to_server_channel.getBlocking();
    defer delivery.unref(); // trace:to_server_channel
    switch (delivery.event) {
        .client_to_server_message => |*incoming_delivery| {
            try server_logic.handleRequest(
                incoming_delivery.client_id,
                incoming_delivery.message.payload,
            );
        },
        .client_connected => |client_id| {
            try server_logic.handleClientConnected(client_id);
        },
        .client_disconnected => |client_id| {
            try server_logic.handleClientDisconnected(client_id);
        },
    }
}

const RefCountedByteSlice = struct {
    payload: []const u8,
    refcounter: RefCounter = .{},
    is_align_4: bool = false,

    pub fn ref(self: *@This()) void {
        self.refcounter.ref();
    }
    pub fn unref(self: *@This()) void {
        if (self.refcounter.unref()) {
            if (self.is_align_4) {
                const allocated_payload: []align(4) const u8 = @alignCast(self.payload.ptr[0..std.mem.alignForward(usize, self.payload.len, 4)]);
                g.gpa.free(allocated_payload);
            } else {
                g.gpa.free(self.payload);
            }
            // later nerds
            g.gpa.destroy(self);
        }
    }
};
