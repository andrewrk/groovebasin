const std = @import("std");

pub fn main() !void {
    var arena_allocator = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena_allocator.deinit();
    const args = try std.process.argsAlloc(arena_allocator.allocator());

    var src_dir = try std.fs.cwd().openDir(args[1], .{});
    defer src_dir.close();

    var fout = try std.fs.cwd().createFile("public/app.js", .{});
    defer fout.close();
    const output = fout.writer();

    try output.writeAll(
        \\(function(modules) {
        \\    function require(name) {
        \\        if (name in initializedModules) {
        \\            return initializedModules[name];
        \\        }
        \\        return initializedModules[name] = modules[name](require);
        \\    }
        \\    var initializedModules = {};
        \\
        \\    for (var name in modules) {
        \\        if (!(name in initializedModules)) {
        \\            modules[name](require);
        \\        }
        \\    }
        \\})({
        \\
    );

    for (args[2..]) |arg| {
        if (!std.mem.endsWith(u8, arg, ".js")) return error.BadFileExtension;
        var name = arg[0 .. arg.len - ".js".len];

        var fin = try src_dir.openFile(arg, .{});
        defer fin.close();

        var things = [_]std.os.iovec_const{
            strToIovec("    \""),
            strToIovec(name),
            strToIovec("\": function(require) {\n"),
            // file contents here.
            strToIovec("    },\n"),
        };
        try fout.writeFileAll(fin, .{
            .headers_and_trailers = &things,
            .header_count = 3,
        });
    }

    try output.writeAll(
        \\});
        \\
    );

    // We're done. No need to run deferred deinitializers.
    if (true) std.process.exit(0);
}

fn strToIovec(s: []const u8) std.os.iovec_const {
    return .{
        .iov_base = s.ptr,
        .iov_len = s.len,
    };
}
