const std = @import("std");
const mem = std.mem;
const net = std.net;
const fs = std.fs;
const os = std.os;
const json = std.json;
const Allocator = std.mem.Allocator;
const Groove = @import("groove.zig").Groove;

const protocol = @import("shared").protocol;
const library = @import("library.zig");
const g = @import("global.zig");

pub fn fatal(comptime format: []const u8, args: anytype) noreturn {
    std.log.err(format, args);
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
    g.gpa = &gpa_instance.allocator;

    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    const arena = &arena_instance.allocator;

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
            fatal("Unable to read {s}: {s}", .{ config_json_path, e });
        },
    };
    var token_stream = json.TokenStream.init(json_text);
    @setEvalBranchQuota(5000);
    const config = try json.parse(ConfigJson, &token_stream, .{ .allocator = arena });

    return listen(.{
        .config = config,
        .arena = arena,
    });
}

fn defaultMusicPath(arena: *Allocator) ![]const u8 {
    if (std.os.getenvZ("XDG_MUSIC_DIR")) |xdg_path| return xdg_path;

    if (std.os.getenv("HOME")) |home| {
        return try fs.path.join(arena, &.{ home, "music" });
    }

    return "music";
}

const ServerInit = struct {
    arena: *Allocator,
    config: ConfigJson,
};

fn listen(server_init: ServerInit) !void {
    const arena = server_init.arena;
    const config = server_init.config;
    const music_dir_path = config.musicDirectory orelse try defaultMusicPath(arena);

    std.log.debug("music directory: {s}", .{music_dir_path});

    Groove.set_logging(.INFO);
    std.log.debug("libgroove version: {s}", .{Groove.version()});

    const groove = try Groove.create();
    g.groove = groove;

    try library.libraryMain(music_dir_path, config.dbPath);

    var server = net.StreamServer.init(.{ .reuse_address = true });
    defer server.deinit();

    const addr = net.Address.parseIp(config.host, config.port) catch |err| {
        fatal("unable to parse {s}:{d}: {s}", .{ config.host, config.port, @errorName(err) });
    };
    try server.listen(addr);
    std.debug.warn("listening at {}\n", .{server.listen_address});

    while (true) {
        const handler = c: {
            const handler = try g.gpa.create(ConnectionHandler);
            errdefer g.gpa.destroy(handler);
            handler.* = .{
                .arena_allocator = std.heap.ArenaAllocator.init(g.gpa),
                .connection = try server.accept(),
            };
            break :c handler;
        };
        errdefer handler.connection.stream.close();
        _ = std.Thread.spawn(.{}, ConnectionHandler.run, .{handler}) catch |err| {
            std.log.err("handling connection failed: {}", .{err});
            continue;
        };
    }
}

const ConnectionHandler = struct {
    arena_allocator: std.heap.ArenaAllocator,
    connection: net.StreamServer.Connection,

    fn arena(handler: *ConnectionHandler) *Allocator {
        return &handler.arena_allocator.allocator;
    }

    fn run(handler: *ConnectionHandler) void {
        handler.handleConnection() catch |err| {
            std.log.err("unable to handle connection: {s}", .{@errorName(err)});
        };
        handler.connection.stream.close();
        handler.arena_allocator.deinit();
    }

    fn handleConnection(handler: *ConnectionHandler) !void {
        var buf: [0x4000]u8 = undefined;
        const amt = try handler.connection.stream.read(&buf);
        const msg = buf[0..amt];
        var header_lines = std.mem.split(u8, msg, "\r\n");
        const first_line = header_lines.next() orelse return;

        // TODO: read the spec
        // eg: "GET /favicon.png HTTP/1.1"
        var it = std.mem.tokenize(u8, first_line, " \t");
        const method = it.next() orelse return;
        const path = it.next() orelse return;
        const http_version = it.next() orelse return;

        // only support GET for HTTP/1.1
        if (!std.mem.eql(u8, method, "GET")) return;
        if (!std.mem.eql(u8, http_version, "HTTP/1.1")) return;

        // TODO: read the other spec
        var sec_websocket_key: ?[]const u8 = null;
        var should_upgrade_websocket: bool = false;
        // TODO: notice when gzip is supported.
        while (header_lines.next()) |line| {
            if (line.len == 0) break;
            var segments = std.mem.split(u8, line, ": ");
            const key = segments.next().?;
            const value = segments.rest();

            if (std.ascii.eqlIgnoreCase(key, "Sec-WebSocket-Key")) {
                sec_websocket_key = value;
            } else if (std.ascii.eqlIgnoreCase(key, "Upgrade")) {
                if (!std.mem.eql(u8, value, "websocket")) return;
                should_upgrade_websocket = true;
            }
        }

        if (should_upgrade_websocket) {
            const websocket_key = sec_websocket_key orelse return;
            std.log.info("GET websocket: {s}", .{path});
            try serveWebsocket(handler.connection, websocket_key, handler.arena());
        } else {
            std.log.info("GET: {s}", .{path});
            try handler.connection.stream.writer().writeAll(try resolvePath(path));
        }
    }
};

const http_response_header_html = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: text/html\r\n" ++
    "\r\n";
const http_response_header_css = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: text/css\r\n" ++
    "\r\n";
const http_response_header_javascript = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: application/javascript\r\n" ++
    "\r\n";
const http_response_header_png = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: image/png\r\n" ++
    "\r\n";
const http_response_header_wasm = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: application/wasm\r\n" ++
    "\r\n";

const http_response_not_found = "" ++
    "HTTP/1.1 404 Not Found\r\n" ++
    "\r\n";

fn resolvePath(path: []const u8) ![]const u8 {
    if (std.mem.eql(u8, path, "/")) return http_response_header_html ++ @embedFile("../public/index.html");
    if (std.mem.eql(u8, path, "/app.css")) return http_response_header_css ++ @embedFile("../public/app.css");
    if (std.mem.eql(u8, path, "/app.js")) return http_response_header_javascript ++ @embedFile("../public/app.js");
    inline for ([_][]const u8{
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
    }) |img_path| {
        if (std.mem.eql(u8, path, img_path)) return http_response_header_png ++ @embedFile("../public" ++ img_path);
    }

    if (std.mem.eql(u8, path, "/client.wasm")) return http_response_header_wasm ++ @embedFile(@import("build_options").client_wasm_path);

    return http_response_not_found;
}

const http_response_header_upgrade = "" ++
    "HTTP/1.1 101 Switching Protocols\r\n" ++
    "Upgrade: websocket\r\n" ++
    "Connection: Upgrade\r\n";

const max_payload_size = 16 * 1024;

fn serveWebsocket(connection: net.StreamServer.Connection, key: []const u8, arena: *Allocator) !void {
    // See https://tools.ietf.org/html/rfc6455
    var sha1 = std.crypto.hash.Sha1.init(.{});
    sha1.update(key);
    sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    var digest: [std.crypto.hash.Sha1.digest_length]u8 = undefined;
    sha1.final(&digest);
    var base64_digest: [28]u8 = undefined;
    std.debug.assert(std.base64.standard_encoder.encode(&base64_digest, &digest).len == base64_digest.len);

    var iovecs = [_]std.os.iovec_const{
        strToIovec(http_response_header_upgrade),
        strToIovec("Sec-WebSocket-Accept: "),
        strToIovec(&base64_digest),
        strToIovec("\r\n" ++ "\r\n"),
    };
    try connection.stream.writevAll(&iovecs);

    while (true) {
        var payload_buffer: [max_payload_size]u8 align(4) = [_]u8{0} ** max_payload_size;
        const payload = (try readMessage(connection, &payload_buffer)) orelse break;
        std.log.info("request: {s}", .{payload});
        const request = try json.parse(protocol.Request, &json.TokenStream.init(payload), json.ParseOptions{ .allocator = arena });

        const response = protocol.Response{
            .seq = request.seq,
            .data = try handleRequest(request.op, request.data),
        };

        var out_buffer: [0x1000]u8 = undefined;
        var fixed_buffer_stream = std.io.fixedBufferStream(&out_buffer);
        const writer = fixed_buffer_stream.writer();
        try json.stringify(response, json.StringifyOptions{}, writer);
        std.log.info("response: {s}", .{fixed_buffer_stream.getWritten()});

        try writeMessage(connection, fixed_buffer_stream.getWritten());
    }
}

fn readMessage(connection: net.StreamServer.Connection, payload_buffer: *align(4) [max_payload_size]u8) !?[]u8 {
    // See https://tools.ietf.org/html/rfc6455
    // read first byte.
    var header = [_]u8{0} ** 2;
    connection.stream.reader().readNoEof(header[0..]) catch |err| switch (err) {
        error.EndOfStream => return null,
        else => return err,
    };
    const opcode_byte = header[0];
    // 0b10000000: FIN - this is a complete message.
    // 0b00000010: opcode=2 - this is a binary message.
    const expected_opcode_byte = 0b10000010;
    if (opcode_byte != expected_opcode_byte) {
        std.log.warn("bad opcode byte: {}", .{opcode_byte});
        return null;
    }

    // read length
    const short_len_byte = header[1];
    if (short_len_byte & 0b10000000 != 0b10000000) {
        std.log.warn("frames from client must be masked: {}", .{short_len_byte});
        return null;
    }
    var len: u64 = switch (short_len_byte & 0b01111111) {
        127 => blk: {
            var len_buffer = [_]u8{0} ** 8;
            try connection.stream.reader().readNoEof(len_buffer[0..]);
            break :blk std.mem.readIntBig(u64, &len_buffer);
        },
        126 => blk: {
            var len_buffer = [_]u8{0} ** 2;
            try connection.stream.reader().readNoEof(len_buffer[0..]);
            break :blk std.mem.readIntBig(u16, &len_buffer);
        },
        else => |short_len| blk: {
            break :blk short_len;
        },
    };
    if (len > max_payload_size) {
        std.log.warn("payload too big: {}", .{len});
        return null;
    }

    // read mask
    var mask_buffer = [_]u8{0} ** 4;
    try connection.stream.reader().readNoEof(mask_buffer[0..]);
    const mask_native = std.mem.readIntNative(u32, &mask_buffer);

    // read payload
    const payload = payload_buffer[0..len];
    try connection.stream.reader().readNoEof(payload);

    // unmask
    // The last item may contain a partial word of unused data.
    const payload_aligned: []u32 = std.mem.bytesAsSlice(u32, payload_buffer[0..std.mem.alignForward(len, 4)]);
    {
        var i: usize = 0;
        while (i < payload_aligned.len) : (i += 1) {
            payload_aligned[i] ^= mask_native;
        }
    }
    return payload;
}

fn writeMessage(connection: net.StreamServer.Connection, message: []const u8) !void {
    // See https://tools.ietf.org/html/rfc6455
    var header_buf: [2 + 8]u8 = undefined;
    // 0b10000000: FIN - this is a complete message.
    // 0b00000010: opcode=2 - this is a binary message.
    header_buf[0] = 0b10000010;
    const header = switch (message.len) {
        0...125 => blk: {
            // small size
            header_buf[1] = @intCast(u8, message.len);
            break :blk header_buf[0..2];
        },
        126...0xffff => blk: {
            // 16-bit size
            header_buf[1] = 126;
            std.mem.writeIntBig(u16, header_buf[2..4], @intCast(u16, message.len));
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
    try connection.stream.writevAll(&iovecs);
}

fn strToIovec(s: []const u8) std.os.iovec_const {
    return .{
        .iov_base = s.ptr,
        .iov_len = s.len,
    };
}

fn handleRequest(op: protocol.Opcode, data: ?protocol.QueryRequest) !protocol.ResponseData {
    switch (op) {
        .ping => {
            var ts: os.timespec = undefined;
            try std.os.clock_gettime(os.CLOCK.REALTIME, &ts);
            return protocol.ResponseData{
                .ping = protocol.Timestamp{
                    .s = ts.tv_sec,
                    .ns = @intCast(i32, ts.tv_nsec),
                },
            };
        },
        .query => {
            const query_request = data.?;
            _ = query_request;
            return protocol.ResponseData{ .query = .{
                .new_library = protocol.Library{
                    .version = 4,
                    .tracks = &[_]protocol.Track{
                        protocol.Track{
                            .id = 9345621390874652103,
                            .title = "adf",
                            .artist = "dfsa",
                            .album = "dosfin",
                        },
                    },
                },
            } };
        },
    }
}
