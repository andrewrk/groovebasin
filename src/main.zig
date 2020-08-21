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
    const first_line = std.mem.split(msg, "\r\n").next() orelse return;

    // TODO: read the spec
    // eg: "GET /favicon.png HTTP/1.1"
    var it = std.mem.tokenize(first_line, " \t");
    const method = it.next() orelse return;
    const path = it.next() orelse return;
    const http_version = it.next() orelse return;

    if (!std.mem.eql(u8, method, "GET")) return;
    if (!std.mem.eql(u8, http_version, "HTTP/1.1")) return;

    std.log.info("GET: {}", .{path});

    try connection.file.writeAll(try resolvePath(path));
}

const http_response_header = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: text/html\r\n" ++
    "\r\n";
const http_response_header_compressed = "" ++
    "HTTP/1.1 200 OK\r\n" ++
    "Content-Type: text/html\r\n" ++
    "Content-Encoding: gzip\r\n" ++
    "\r\n";

fn resolvePath(path: []const u8) ![]const u8 {
    if (std.mem.eql(u8, path, "/")) return http_response_header ++ @embedFile("./public/index.html");
    if (std.mem.eql(u8, path, "/app.css")) return http_response_header_compressed ++ @embedFile("./public/app.css");
    if (std.mem.eql(u8, path, "/app.js")) return http_response_header_compressed ++ @embedFile("./public/app.js");
    if (std.mem.eql(u8, path, "/favicon.png")) return http_response_header ++ @embedFile("./public/favicon.png");
    return error.NotFound;
}
