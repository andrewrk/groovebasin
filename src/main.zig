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
        var buf: [0x4000]u8 = undefined;
        const amt = try connection.file.read(&buf);
        const msg = buf[0..amt];
        std.debug.print("{}\n", .{msg});

        try connection.file.writeAll("" ++
            "HTTP/1.1 200 OK\r\n" ++
            "Content-Type: text/html\r\n" ++
            "\r\n" ++
            "<html><body><select id=\"organize\"><option selected=\"selected\">Artist / Album / Song</option></select></body></html>\r\n" ++
            "\r\n");
    }
}
