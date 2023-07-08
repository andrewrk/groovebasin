const std = @import("std");
const mem = std.mem;
const net = std.net;
const fs = std.fs;
const os = std.os;
const json = std.json;
const log = std.log;
const Allocator = std.mem.Allocator;
const Mutex = std.Thread.Mutex;
const Groove = @import("groove.zig").Groove;
const SoundIo = @import("soundio.zig").SoundIo;
const Player = @import("Player.zig");

const protocol = @import("shared").protocol;
const library = @import("library.zig");
const queue = @import("queue.zig");
const events = @import("events.zig");
const g = @import("global.zig");

pub fn fatal(comptime format: []const u8, args: anytype) noreturn {
    log.err(format, args);
    std.process.exit(1);
}

const ConfigJson = struct {
    host: []const u8 = "127.0.0.1",
    port: u16 = 16242,
    dbPath: []const u8 = "groovebasin.db",
    musicDirectory: ?[]const u8 = null,
    lastFmApiKey: []const u8 = "bb9b81026cd44fd086fa5533420ac9b4",
    lastFmApiSecret: []const u8 = "2309a40ae3e271de966bf320498a8f09",
    acoustidAppKey: []const u8 = "bgFvC4vW",
    encodeQueueDuration: f64 = 8,
    encodeBitRate: u32 = 256,
    sslKey: ?[]const u8 = null,
    sslCert: ?[]const u8 = null,
    sslCaDir: ?[]const u8 = null,
    googleApiKey: []const u8 = "AIzaSyDdTDD8-gu_kp7dXtT-53xKcVbrboNAkpM",
    ignoreExtensions: []const []const u8 = &.{
        ".jpg", ".jpeg", ".txt", ".png", ".log", ".cue", ".pdf", ".m3u",
        ".nfo", ".ini",  ".xml", ".zip",
    },
};

const usage =
    \\Usage: groovebasin [options]
    \\Options:
    \\  --config [file]      Defaults to config.json in the cwd
    \\  -h, --help           Print this help menu to stdout
    \\
;

pub fn main() anyerror!void {
    var gpa_instance: std.heap.GeneralPurposeAllocator(.{}) = .{};
    defer _ = gpa_instance.deinit();
    g.gpa = gpa_instance.allocator();

    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    const arena = arena_instance.allocator();

    const args = try std.process.argsAlloc(arena);

    var config_json_path: []const u8 = "config.json";
    var i: usize = 1;
    while (i < args.len) : (i += 1) {
        const arg = args[i];
        if (mem.eql(u8, arg, "--config")) {
            if (i + 1 >= args.len) fatal("expected parameter after {s}", .{arg});
            i += 1;
            config_json_path = args[i];
        } else if (mem.eql(u8, arg, "-h") or mem.eql(u8, arg, "--help")) {
            try std.io.getStdOut().writeAll(usage);
            std.process.exit(0);
        } else {
            fatal("unrecognized argument: '{s}'", .{arg});
        }
    }

    const max_config_size = 1 * 1024 * 1024;
    const json_text = fs.cwd().readFileAlloc(arena, config_json_path, max_config_size) catch |err| switch (err) {
        error.FileNotFound => {
            var atomic_file = try fs.cwd().atomicFile(config_json_path, .{});
            defer atomic_file.deinit();

            var buffered_writer = std.io.bufferedWriter(atomic_file.file.writer());

            const config: ConfigJson = .{
                .musicDirectory = try defaultMusicPath(arena),
            };
            try json.stringify(config, .{ .whitespace = .{} }, buffered_writer.writer());

            try buffered_writer.flush();
            try atomic_file.finish();

            fatal("No {s} found; writing default. Take a peek and make sure the values are to your liking, then start GrooveBasin again.", .{config_json_path});
        },
        else => |e| {
            fatal("Unable to read {s}: {s}", .{ config_json_path, @errorName(e) });
        },
    };
    const config = try json.parseFromSliceLeaky(ConfigJson, arena, json_text, .{});

    return listen(arena, config);
}

fn defaultMusicPath(arena: Allocator) ![]const u8 {
    if (std.os.getenvZ("XDG_MUSIC_DIR")) |xdg_path| return xdg_path;

    if (std.os.getenv("HOME")) |home| {
        return try fs.path.join(arena, &.{ home, "music" });
    }

    return "music";
}

fn listen(arena: Allocator, config: ConfigJson) !void {
    const music_dir_path = config.musicDirectory orelse try defaultMusicPath(arena);

    log.info("music directory: {s}", .{music_dir_path});

    log.debug("libgroove version: {s}", .{Groove.version()});

    const soundio = try SoundIo.create();
    g.soundio = soundio;
    g.soundio.app_name = "GrooveBasin";
    g.soundio.connect_backend(.Dummy) catch |err| fatal("unable to initialize sound: {s}", .{@errorName(err)});
    g.soundio.flush_events();

    const groove = try Groove.create();
    Groove.set_logging(.INFO);
    g.groove = groove;

    var player = try Player.init(config.encodeBitRate);
    defer player.deinit();

    std.log.info("init library", .{});
    try library.init(music_dir_path, config.dbPath);
    defer library.deinit();

    std.log.info("init queue", .{});
    try queue.init();
    defer queue.deinit();

    std.log.info("init events", .{});
    try events.init();
    defer events.deinit();

    std.log.info("init static content", .{});
    try init_static_content();

    std.log.info("init stream server", .{});
    var server = net.StreamServer.init(.{ .reuse_address = true });
    defer server.deinit();

    const addr = net.Address.parseIp(config.host, config.port) catch |err| {
        fatal("unable to parse {s}:{d}: {s}", .{ config.host, config.port, @errorName(err) });
    };
    try server.listen(addr);
    std.log.info("listening at {}", .{server.listen_address});

    client_connections = Connections.init(g.gpa);

    while (true) {
        const handler = c: {
            const handler = try g.gpa.create(ConnectionHandler);
            errdefer g.gpa.destroy(handler);
            handler.* = .{
                .arena_allocator = std.heap.ArenaAllocator.init(g.gpa),
                .connection = try server.accept(),
                .player = &player,
            };
            break :c handler;
        };
        errdefer handler.connection.stream.close();
        _ = std.Thread.spawn(.{}, ConnectionHandler.run, .{handler}) catch |err| {
            log.err("handling connection failed: {}", .{err});
            continue;
        };
    }
}

const Connections = std.AutoHashMap(*ConnectionHandler, void);
var client_connections: Connections = undefined;
var client_connections_mutex = Mutex{};

const ConnectionHandler = struct {
    arena_allocator: std.heap.ArenaAllocator,
    connection: net.StreamServer.Connection,
    player: *Player,

    // only used for websocket handlers
    websocket_send_queue: WebsocketReadQueue = WebsocketReadQueue.init(),
    websocket_send_thread: std.Thread = undefined,
    websocket_send_thread_shutdown: bool = false,

    const WebsocketReadQueue = std.atomic.Queue([]u8);

    fn arena(handler: *ConnectionHandler) Allocator {
        return handler.arena_allocator.allocator();
    }
    fn resetArena(handler: *ConnectionHandler) void {
        // TODO: get this into the stdlib?
        handler.arena_allocator.deinit();
        handler.arena_allocator.state = .{};
    }

    fn run(handler: *ConnectionHandler) void {
        handler.handleConnection() catch |err| {
            log.err("unable to handle connection: {s}", .{@errorName(err)});
        };
        handler.connection.stream.close();
        handler.arena_allocator.deinit();
        g.gpa.destroy(handler);
    }

    fn handleConnection(handler: *ConnectionHandler) !void {
        // TODO: this method of reading headers can read too many bytes.
        // We should use a buffered reader and put back any content after the headers.
        var buf: [0x4000]u8 = undefined;
        const msg = buf[0..try handler.connection.stream.read(&buf)];
        var header_lines = std.mem.split(u8, msg, "\r\n");
        const first_line = header_lines.next() orelse return error.NotAnHttpRequest;

        // eg: "GET /favicon.png HTTP/1.1"
        var it = std.mem.tokenize(u8, first_line, " \t");
        const method = it.next() orelse return error.NotAnHttpRequest;
        const path = it.next() orelse return error.NotAnHttpRequest;
        const http_version = it.next() orelse return error.NotAnHttpRequest;

        // Only support GET for HTTP/1.1
        if (!std.mem.eql(u8, method, "GET")) return error.UnsupportedHttpMethod;
        if (!std.mem.eql(u8, http_version, "HTTP/1.1")) return error.UnsupportedHttpVersion;

        // Find interesting headers.
        var sec_websocket_key: ?[]const u8 = null;
        var should_upgrade_websocket: bool = false;
        while (header_lines.next()) |line| {
            if (line.len == 0) break;
            var segments = std.mem.split(u8, line, ": ");
            const key = segments.next().?;
            const value = segments.rest();

            if (std.ascii.eqlIgnoreCase(key, "Sec-WebSocket-Key")) {
                sec_websocket_key = value;
            } else if (std.ascii.eqlIgnoreCase(key, "Upgrade")) {
                if (!std.mem.eql(u8, value, "websocket")) return error.UnsupportedProtocolUpgrade;
                should_upgrade_websocket = true;
            }
        }

        if (should_upgrade_websocket) {
            const websocket_key = sec_websocket_key orelse return error.WebsocketUpgradeMissingSecKey;
            log.info("GET websocket: {s}", .{path});
            // This is going to stay open for a long time.
            return handler.serveWebsocket(websocket_key);
        }

        log.info("GET: {s}", .{path});

        if (mem.eql(u8, path, "/stream.mp3")) {
            // This is going to stay open for a long time.
            return handler.streamEndpoint();
        }

        // Getting static content
        return serveStaticFile(&handler.connection, path);
    }

    fn streamEndpoint(handler: *ConnectionHandler) !void {
        const response_header =
            "HTTP/1.1 200 OK\r\n" ++
            "Content-Type: audio/mpeg\r\n" ++
            "Cache-Control: no-cache, no-store, must-revalidate\r\n" ++
            "Pragma: no-cache\r\n" ++
            "Expires: 0\r\n" ++
            "\r\n";

        const w = handler.connection.stream.writer();
        try w.writeAll(response_header);

        while (true) {
            var buffer: ?*Groove.Buffer = null;
            const status = try handler.player.encoder.buffer_get(&buffer, true);
            _ = status;
            if (buffer) |buf| {
                const data = buf.data[0][0..@as(usize, @intCast(buf.size))];
                try w.writeAll(data);
                buf.unref();
            }
        }
    }

    fn serveWebsocket(handler: *ConnectionHandler, key: []const u8) !void {
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
            try handler.connection.stream.writevAll(&iovecs);
        }

        handler.websocket_send_thread = std.Thread.spawn(.{}, ConnectionHandler.websocketSendLoop, .{handler}) catch |err| {
            log.err("spawning websocket handler thread failed: {}", .{err});
            return err;
        };
        defer {
            handler.websocket_send_thread_shutdown = true;
            handler.websocket_send_thread.join();
        }

        // Welcome to the party.
        {
            client_connections_mutex.lock();
            defer client_connections_mutex.unlock();
            try client_connections.putNoClobber(handler, {});
            std.log.info("client connections count++: {}", .{client_connections.count()});
        }
        defer {
            // Goodbye from the party.
            client_connections_mutex.lock();
            defer client_connections_mutex.unlock();
            if (!client_connections.remove(handler)) unreachable;
            std.log.info("client connections count--: {}", .{client_connections.count()});
        }

        while (true) {
            handler.resetArena();

            const request_payload = (try handler.readMessage()) orelse break;
            log.info("request: {s}", .{request_payload});

            const message = try std.json.parseFromSliceLeaky(Message, handler.arena(), request_payload, .{});
            const response = try handler.handleRequest(message);
            const response_payload = try std.json.stringifyAlloc(handler.arena(), response, .{});

            log.info("response: {s}", .{response_payload});
            try handler.queueSendMessage(response_payload);
        }
    }

    fn readMessage(handler: *ConnectionHandler) !?[]u8 {
        // See https://tools.ietf.org/html/rfc6455
        // read first byte.
        var header = [_]u8{0} ** 2;
        handler.connection.stream.reader().readNoEof(header[0..]) catch |err| switch (err) {
            error.EndOfStream => return null,
            else => return err,
        };
        const opcode_byte = header[0];
        // 0b10000000: FIN - this is a complete message.
        // 0b00000001: opcode=1 - this is a text message.
        const expected_opcode_byte = 0b10000001;
        if (opcode_byte != expected_opcode_byte) {
            log.warn("bad opcode byte: {}", .{opcode_byte});
            return null;
        }

        // read length
        const short_len_byte = header[1];
        if (short_len_byte & 0b10000000 != 0b10000000) {
            log.warn("frames from client must be masked: {}", .{short_len_byte});
            return null;
        }
        var len: u64 = switch (short_len_byte & 0b01111111) {
            127 => blk: {
                var len_buffer = [_]u8{0} ** 8;
                try handler.connection.stream.reader().readNoEof(len_buffer[0..]);
                break :blk std.mem.readIntBig(u64, &len_buffer);
            },
            126 => blk: {
                var len_buffer = [_]u8{0} ** 2;
                try handler.connection.stream.reader().readNoEof(len_buffer[0..]);
                break :blk std.mem.readIntBig(u16, &len_buffer);
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
        try handler.connection.stream.reader().readNoEof(mask_buffer[0..]);
        const mask_native = std.mem.readIntNative(u32, &mask_buffer);

        // read payload
        const payload_aligned = try handler.arena().allocWithOptions(u8, std.mem.alignForward(usize, len, 4), 4, null);
        const payload = payload_aligned[0..len];
        try handler.connection.stream.reader().readNoEof(payload);

        // unmask
        // The last item may contain a partial word of unused data.
        const payload_as_u32_array: []u32 = std.mem.bytesAsSlice(u32, payload_aligned);
        {
            var i: usize = 0;
            while (i < payload_as_u32_array.len) : (i += 1) {
                payload_as_u32_array[i] ^= mask_native;
            }
        }
        return payload;
    }

    fn queueSendMessage(handler: *ConnectionHandler, message: []const u8) !void {
        if (handler.websocket_send_thread_shutdown) {
            // There's surely some kind of race condition memory leak here.
            return;
        }
        const node = try g.gpa.create(WebsocketReadQueue.Node);
        node.* = WebsocketReadQueue.Node{
            .data = try g.gpa.dupe(u8, message),
        };
        handler.websocket_send_queue.put(node);
    }

    fn websocketSendLoop(handler: *ConnectionHandler) void {
        while (true) {
            const node = handler.websocket_send_queue.get() orelse {
                if (handler.websocket_send_thread_shutdown) return;
                std.time.sleep(17_000_000);
                continue;
            };
            const message = node.data;
            defer {
                g.gpa.free(message);
                g.gpa.destroy(node);
            }
            handler.writeMessageFromSendThread(message) catch |err| switch (err) {
                error.BrokenPipe => {
                    log.warn("websocket closed unexpectedly: {}", .{err});
                    handler.websocket_send_thread_shutdown = true;
                },
                else => {
                    log.err("error writing message to websocket: {}", .{err});
                },
            };
        }
    }

    fn writeMessageFromSendThread(handler: *ConnectionHandler, message: []const u8) !void {
        // See https://tools.ietf.org/html/rfc6455
        var header_buf: [2 + 8]u8 = undefined;
        // 0b10000000: FIN - this is a complete message.
        // 0b00000010: opcode=2 - this is a binary message.
        header_buf[0] = 0b10000010;
        const header = switch (message.len) {
            0...125 => blk: {
                // small size
                header_buf[1] = @as(u8, @intCast(message.len));
                break :blk header_buf[0..2];
            },
            126...0xffff => blk: {
                // 16-bit size
                header_buf[1] = 126;
                std.mem.writeIntBig(u16, header_buf[2..4], @as(u16, @intCast(message.len)));
                break :blk header_buf[0..4];
            },
            else => blk: {
                // 64-bit size
                header_buf[1] = 127;
                std.mem.writeIntBig(u64, header_buf[2..10], message.len);
                break :blk header_buf[0..10];
            },
        };

        var iovecs = [_]std.os.iovec_const{
            strToIovec(header),
            strToIovec(message),
        };
        try handler.connection.stream.writevAll(&iovecs);
    }

    fn handleRequest(handler: *ConnectionHandler, request: Message) !std.json.Value {
        _ = handler;
        switch (request.name) {
            .setStreaming => {
                // TODO
                return .null;
            },
            .subscribe => {
                // TODO
                return .null;
            },
        }
    }
};

const http_response_not_found = "" ++
    "HTTP/1.1 404 Not Found\r\n" ++
    "\r\n";

const StaticFile = struct {
    entire_response: []const u8,
};
var static_content_map: std.StringHashMap(StaticFile) = undefined;
fn init_static_content() !void {
    static_content_map = std.StringHashMap(StaticFile).init(g.gpa);

    // TODO: resolve relative to current executable maybe?
    var static_content_dir = try std.fs.cwd().openDir("zig-out/lib/public", .{});
    defer static_content_dir.close();

    for ([_][]const u8{
        "/",
        "/app.js",
        "/favicon.png",
        "/img/ui-icons_ffffff_256x240.png",
        "/img/ui-bg_dots-small_30_a32d00_2x2.png",
        "/img/ui-bg_flat_0_aaaaaa_40x100.png",
        "/img/ui-bg_dots-medium_30_0b58a2_4x4.png",
        "/img/ui-icons_98d2fb_256x240.png",
        "/img/ui-icons_00498f_256x240.png",
        "/img/ui-bg_gloss-wave_20_111111_500x100.png",
        "/img/bright-10.png",
        "/img/ui-icons_9ccdfc_256x240.png",
        "/img/ui-bg_dots-small_40_00498f_2x2.png",
        "/img/ui-bg_dots-small_20_333333_2x2.png",
        "/img/ui-bg_diagonals-thick_15_0b3e6f_40x40.png",
        "/img/ui-bg_flat_40_292929_40x100.png",
    }) |path| {
        try static_content_map.putNoClobber(path, try resolveStaticFile(static_content_dir, path));
    }
}

fn resolveStaticFile(static_content_dir: std.fs.Dir, path: []const u8) !StaticFile {
    var mime_type: []const u8 = undefined;
    var relative_path: []const u8 = path[1..];
    if (std.mem.eql(u8, path, "/")) {
        mime_type = "text/html";
        relative_path = "index.html";
    } else if (std.mem.endsWith(u8, path, ".css")) {
        mime_type = "text/css";
    } else if (std.mem.endsWith(u8, path, ".js")) {
        mime_type = "application/javascript";
    } else if (std.mem.endsWith(u8, path, ".png")) {
        mime_type = "image/png";
    } else unreachable;

    var file = try static_content_dir.openFile(relative_path, .{});
    defer file.close();
    const contents = try file.reader().readAllAlloc(g.gpa, 100_000_000);
    defer g.gpa.free(contents);

    return StaticFile{
        .entire_response = try std.fmt.allocPrint(g.gpa, "" ++
            "HTTP/1.1 200 OK\r\n" ++
            "Content-Type: {s}\r\n" ++
            "Content-Length: {d}\r\n" ++
            "\r\n" ++
            "{s}", .{
            mime_type,
            contents.len,
            contents,
        }),
    };
}

fn serveStaticFile(connection: *net.StreamServer.Connection, path: []const u8) !void {
    const static_file = static_content_map.get(path) orelse {
        std.log.warn("not found: {s}", .{path});
        return connection.stream.writer().writeAll(http_response_not_found);
    };
    try connection.stream.writeAll(static_file.entire_response);
}

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

fn broadcastPushMessage() !void {
    const entire_message = protocol.ResponseHeader{
        .seq_id = 0x8000_0000,
    };
    try broadcastMessage(std.mem.asBytes(&entire_message));
}

fn broadcastMessage(message: []const u8) !void {
    client_connections_mutex.lock();
    defer client_connections_mutex.unlock();

    log.info("broadcast: {s}", .{std.fmt.fmtSliceHexLower(message)});

    var it = client_connections.iterator();
    while (it.next()) |entry| {
        const handler = entry.key_ptr.*;
        try handler.queueSendMessage(message);
    }
}

// TODO: move this elsewhere:
const Message = struct {
    name: enum {
        setStreaming,
        subscribe,
    },
    args: std.json.Value,
};
