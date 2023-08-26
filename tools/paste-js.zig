const std = @import("std");

pub fn main() !void {
    var arena_allocator = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena_allocator.deinit();
    const args = try std.process.argsAlloc(arena_allocator.allocator());

    const out_file_name = args[1];
    var fout = try std.fs.cwd().createFile(out_file_name, .{});
    defer fout.close();
    const output = fout.writer();

    try output.writeAll(
        \\(function(modules) {
        \\    var initializedModules = {};
        \\    function require(name) {
        \\        if (name in initializedModules) {
        \\            return initializedModules[name];
        \\        }
        \\        var module = {exports: {}}
        \\        modules[name](require, module, module.exports);
        \\        return initializedModules[name] = module.exports;
        \\    }
        \\
        \\    for (var name in modules) {
        \\        require(name);
        \\    }
        \\})({
        \\
    );

    for (args[2..]) |arg| {
        if (!std.mem.endsWith(u8, arg, ".js")) return error.BadFileExtension;
        const basename = std.fs.path.basename(arg);
        const name = basename[0 .. basename.len - ".js".len];

        var fin = try std.fs.cwd().openFile(arg, .{});
        defer fin.close();

        var things = [_]std.os.iovec_const{
            strToIovec("    \""),
            strToIovec(name),
            strToIovec("\": function(require, module, exports) {\n"),
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

    return std.process.cleanExit();
}

fn strToIovec(s: []const u8) std.os.iovec_const {
    return .{
        .iov_base = s.ptr,
        .iov_len = s.len,
    };
}
