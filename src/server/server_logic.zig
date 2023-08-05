const std = @import("std");
const Allocator = std.mem.Allocator;
const ArenaAllocator = std.heap.ArenaAllocator;
const log = std.log;

const g = @import("global.zig");

const groovebasin_protocol = @import("groovebasin_protocol.zig");
const sendMessageToClient = @import("web_server.zig").sendMessageToClient;

pub fn handleClientConnected(client_id: *anyopaque) !void {
    _ = client_id;
}
pub fn handleClientDisconnected(client_id: *anyopaque) !void {
    _ = client_id;
}

fn parseMessage(allocator: Allocator, message_bytes: []const u8) !groovebasin_protocol.ClientToServerMessage {
    var scanner = std.json.Scanner.initCompleteInput(allocator, message_bytes);
    defer scanner.deinit();
    var diagnostics = std.json.Diagnostics{};
    scanner.enableDiagnostics(&diagnostics);
    return std.json.parseFromTokenSourceLeaky(
        groovebasin_protocol.ClientToServerMessage,
        allocator,
        &scanner,
        .{},
    ) catch |err| {
        log.warn("json err: {}: line,col: {},{}", .{ err, diagnostics.getLine(), diagnostics.getColumn() });
        return err;
    };
}

fn encodeAndSend(client_id: *anyopaque, message: groovebasin_protocol.ServerToClientMessage) !void {
    const message_bytes = try std.json.stringifyAlloc(g.gpa, message, .{});
    try sendMessageToClient(client_id, message_bytes);
}

pub fn handleRequest(client_id: *anyopaque, message_bytes: []const u8) !void {
    var arena = ArenaAllocator.init(g.gpa);
    defer arena.deinit();
    const message = try parseMessage(arena.allocator(), message_bytes);
    // startup messages:
    //  .user
    //  .time
    //  .token
    //  .lastFmApiKey
    //  .user (again, this time as not a guest)
    //  .streamEndpoint
    //  .autoDjOn
    //  .hardwarePlayback
    //  .haveAdminUser
    //  .labels - large database
    //  .library - large database
    //  .queue - large database
    //  .scanning
    //  .volume
    //  .repeat
    //  .currentTrack
    //  .playlists - large database
    //  .anonStreamers
    //  .users (not to be confused with .user)
    //  .events - large database
    //  .importProgress
    switch (message) {
        .setStreaming => {
            // TODO
        },
        .subscribe => {
            // TODO
            try encodeAndSend(client_id, .{
                .library = .{},
            });
        },
        else => unreachable,
    }
}
