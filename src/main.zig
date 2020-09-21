const std = @import("std");
const net = std.net;
const fs = std.fs;
const os = std.os;

// FIXME: seems to be a bug with long writeAll calls.
// pub const io_mode = .evented;

pub fn main() anyerror!void {
    var server = net.StreamServer.init(.{ .reuse_address = true });
    defer server.deinit();

    try server.listen(net.Address.parseIp("127.0.0.1", 8000) catch unreachable);
    std.debug.warn("listening at {}\n", .{server.listen_address});

    while (true) {
        const connection = try server.accept();
        defer connection.file.close();
        handleConnection(connection) catch |err| {
            std.log.err("handling connection failed: {}", .{err});
        };
    }
}

fn handleConnection(connection: std.net.StreamServer.Connection) !void {
    var buf: [0x4000]u8 = undefined;
    const amt = try connection.file.read(&buf);
    const msg = buf[0..amt];
    var header_lines = std.mem.split(msg, "\r\n");
    const first_line = header_lines.next() orelse return;

    // TODO: read the spec
    // eg: "GET /favicon.png HTTP/1.1"
    var it = std.mem.tokenize(first_line, " \t");
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
        var segments = std.mem.split(line, ": ");
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
        std.log.info("GET websocket: {}", .{path});
        try serveWebsocket(connection, websocket_key);
    } else {
        std.log.info("GET: {}", .{path});
        try connection.file.writeAll(try resolvePath(path));
    }
}

const http_response_header_html = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: text/html\r\n" ++
    "\r\n";
const http_response_header_html_compressed = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: text/html\r\n" ++
    "Content-Encoding: gzip\r\n" ++
    "\r\n";
const http_response_header_css_compressed = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: text/css\r\n" ++
    "Content-Encoding: gzip\r\n" ++
    "\r\n";
const http_response_header_javascript = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: application/javascript\r\n" ++
    "\r\n";
const http_response_header_png = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: image/png\r\n" ++
    "\r\n";

const http_response_not_found = "" ++
    "HTTP/1.1 404 Not Found\r\n" ++
    "\r\n";

fn resolvePath(path: []const u8) ![]const u8 {
    if (std.mem.eql(u8, path, "/")) return http_response_header_html ++ @embedFile("./public/index.html");
    if (std.mem.eql(u8, path, "/app.css")) return http_response_header_css_compressed ++ @embedFile("./public/app.css");
    if (std.mem.eql(u8, path, "/app.js")) return http_response_header_javascript ++ @embedFile("./public/app.js");
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
        if (std.mem.eql(u8, path, img_path)) return http_response_header_png ++ @embedFile("./public" ++ img_path);
    }

    return http_response_not_found;
}

const http_response_header_upgrade = "" ++
    "HTTP/1.1 101 Switching Protocols\r\n" ++
    "Upgrade: websocket\r\n" ++
    "Connection: Upgrade\r\n";

fn serveWebsocket(connection: std.net.StreamServer.Connection, key: []const u8) !void {
    var sha1 = std.crypto.hash.Sha1.init(.{});
    sha1.update(key);
    sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    var digest: [std.crypto.hash.Sha1.digest_length]u8 = undefined;
    sha1.final(&digest);
    var base64_digest: [28]u8 = undefined;
    std.base64.standard_encoder.encode(&base64_digest, &digest);

    var iovecs = [_]std.os.iovec_const{
        strToIovec(http_response_header_upgrade),
        strToIovec("Sec-WebSocket-Accept: "),
        strToIovec(&base64_digest),
        strToIovec("\r\n" ++ "\r\n"),
    };
    try connection.file.writevAll(&iovecs);

    while (true) {
        // read first byte.
        var header = [_]u8{0} ** 2;
        readAllNoEof(connection.file, header[0..]) catch |err| switch (err) {
            error.UnexpectedEof => break,
            else => return err,
        };
        const opcode_byte = header[0];
        // 0b10000000: FIN - this is a complete message.
        // 0b00000001: opcode=1 - this is a UTF-8 text message.
        const expected_opcode_byte = 0b10000001;
        if (opcode_byte != expected_opcode_byte) {
            std.log.warn("bad opcode byte: {}", .{opcode_byte});
            return;
        }

        // read length
        const short_len_byte = header[1];
        if (short_len_byte & 0b10000000 != 0b10000000) {
            std.log.warn("frames from client must be masked: {}", .{short_len_byte});
            return;
        }
        var len: u64 = switch (short_len_byte & 0b01111111) {
            127 => blk: {
                var len_buffer = [_]u8{0} ** 8;
                try readAllNoEof(connection.file, len_buffer[0..]);
                break :blk std.mem.readIntBig(u64, &len_buffer);
            },
            126 => blk: {
                var len_buffer = [_]u8{0} ** 2;
                try readAllNoEof(connection.file, len_buffer[0..]);
                break :blk std.mem.readIntBig(u16, &len_buffer);
            },
            else => |short_len| blk: {
                break :blk short_len;
            },
        };
        const max_payload_size = 16 * 1024;
        if (len > max_payload_size) {
            std.log.warn("payload too big: {}", .{len});
            return;
        }

        // read mask
        var mask_buffer = [_]u8{0} ** 4;
        try readAllNoEof(connection.file, mask_buffer[0..]);
        const mask_native = std.mem.readIntNative(u32, &mask_buffer);

        // read payload
        var payload_buffer = [_]u8{0} ** max_payload_size;
        const payload = payload_buffer[0..len];
        try readAllNoEof(connection.file, payload);
        const payload_aligned = payload_buffer[0..std.mem.alignForward(len, 4)];

        // unmask
        {
            var i: usize = 0;
            while (i < payload_aligned.len) : (i += 4) {
                std.mem.writeIntNative(u32, payload_aligned[i..][0..4], mask_native ^ std.mem.readIntNative(u32, payload_aligned[i..][0..4]));
            }
        }

        std.log.info("message: {}", .{payload});
    }
}

fn strToIovec(s: []const u8) std.os.iovec_const {
    return .{
        .iov_base = s.ptr,
        .iov_len = s.len,
    };
}

fn readAllNoEof(file: std.fs.File, buffer: []u8) !void {
    if (buffer.len > try file.readAll(buffer)) return error.UnexpectedEof;
}
