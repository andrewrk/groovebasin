const std = @import("std");

pub fn main() !void {
    var arena_allocator = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena_allocator.deinit();
    const gpa = arena_allocator.allocator();
    const args = try std.process.argsAlloc(gpa);

    const html_source = try std.fs.cwd().readFileAlloc(gpa, args[1], 10_000_000);
    const css_file = try std.fs.cwd().openFile(args[2], .{});

    const token = "<!--CSS_GOES_HERE-->";
    const token_index = std.mem.indexOf(u8, html_source, token).?;

    const output_html_file = args[3];
    var fout = try std.fs.cwd().createFile(output_html_file, .{});
    defer fout.close();

    var things = [_]std.os.iovec_const{
        strToIovec(html_source[0..token_index]),
        strToIovec("<style>"),
        // file contents here.
        strToIovec("</style>"),
        strToIovec(html_source[token_index + token.len ..]),
    };
    try fout.writeFileAll(css_file, .{
        .headers_and_trailers = &things,
        .header_count = 2,
    });
}

fn strToIovec(s: []const u8) std.os.iovec_const {
    return .{
        .iov_base = s.ptr,
        .iov_len = s.len,
    };
}
