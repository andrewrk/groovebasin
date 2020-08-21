const std = @import("std");
const net = std.net;
const fs = std.fs;
const os = std.os;

pub const io_mode = .evented;

var general_purpose_allocator = std.heap.GeneralPurposeAllocator(.{}){};

pub fn main() anyerror!void {
    const allocator = if (std.builtin.link_libc) std.heap.c_allocator else &general_purpose_allocator.allocator;
    defer if (!std.builtin.link_libc) {
        _ = general_purpose_allocator.deinit();
    };

    var server = net.StreamServer.init(.{});
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
const http_response_header_javascript_compressed = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: application/javascript\r\n" ++
    "Content-Encoding: gzip\r\n" ++
    "\r\n";
const http_response_header_png = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: image/png\r\n" ++
    "\r\n";

fn resolvePath(path: []const u8) ![]const u8 {
    if (std.mem.eql(u8, path, "/")) return http_response_header_html ++ @embedFile("./public/index.html");
    if (std.mem.eql(u8, path, "/app.css")) return http_response_header_css_compressed ++ @embedFile("./public/app.css");
    if (std.mem.eql(u8, path, "/app.js")) return http_response_header_javascript_compressed ++ @embedFile("./public/app.js");
    if (std.mem.eql(u8, path, "/favicon.png")) return http_response_header_png ++ @embedFile("./public/favicon.png");
    return error.NotFound;
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

    try connection.file.writeAll(http_response_header_upgrade);
    try connection.file.writeAll("Sec-WebSocket-Accept: ");
    try connection.file.writeAll(&base64_digest);
    try connection.file.writeAll("\r\n" ++ "\r\n");
}
