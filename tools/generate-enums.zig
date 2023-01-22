const std = @import("std");

// Parameters passed by global variable.
// (Shoutouts to single-threaded, single-pass, single-purpose tools!)
var gpa: std.mem.Allocator = undefined;
var http_client: std.http.Client = undefined;

pub fn main() !void {
    var arena_allocator = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena_allocator.deinit();
    gpa = arena_allocator.allocator();

    http_client = .{ .allocator = gpa };
    defer http_client.deinit();

    try generateKeyboardEventCode();
    try generateEventType();
}

fn generateKeyboardEventCode() !void {
    const contents = try downloadEntireUrl("https://developer.mozilla.org/en-US/docs/Web/API/UI_Events/Keyboard_event_code_values");
    defer gpa.free(contents);

    var name_set = std.StringArrayHashMap(void).init(gpa);
    defer name_set.deinit();

    // we're looking for this regex: '<code>"([A-Z][0-9A-Za-z]*)"'
    var state: enum {
        start,
        lt,
        lt_c,
        lt_co,
        lt_cod,
        lt_code,
        lt_code_gt,
        lt_code_gt_quot,
        lt_code_gt_quot_name,
    } = .start;
    var name_start: usize = 0;
    for (contents) |c, i| {
        switch (state) {
            .start => state = if (c == '<') .lt else .start,
            .lt => state = if (c == 'c') .lt_c else .start,
            .lt_c => state = if (c == 'o') .lt_co else .start,
            .lt_co => state = if (c == 'd') .lt_cod else .start,
            .lt_cod => state = if (c == 'e') .lt_code else .start,
            .lt_code => state = if (c == '>') .lt_code_gt else .start,
            .lt_code_gt => state = if (c == '"') .lt_code_gt_quot else .start,
            .lt_code_gt_quot => switch (c) {
                'A'...'Z' => {
                    state = .lt_code_gt_quot_name;
                    name_start = i;
                },
                else => {
                    state = .start;
                },
            },
            .lt_code_gt_quot_name => switch (c) {
                '0'...'9', 'A'...'Z', 'a'...'z' => {},
                '"' => {
                    _ = try name_set.put(contents[name_start..i], {});
                    state = .start;
                },
                else => {
                    state = .start;
                },
            },
        }
    }

    name_set.sort(struct {
        keys: [][]const u8,
        pub fn lessThan(ctx: @This(), a_index: usize, b_index: usize) bool {
            return std.mem.lessThan(u8, ctx.keys[a_index], ctx.keys[b_index]);
        }
    }{ .keys = name_set.keys() });

    try writeZigEnum("src/client/zig/_generated_KeyboardEventCode.zig", "KeyboardEventCode", name_set.keys());
    try writeJsEnum("src/client/js/_generated_KeyboardEventCode.js", name_set.keys());
}

fn generateEventType() !void {
    const contents = try downloadEntireUrl("https://developer.mozilla.org/en-US/docs/Web/Events");
    defer gpa.free(contents);

    var name_set = std.StringArrayHashMap(void).init(gpa);
    defer name_set.deinit();

    // we're looking for this regex: '>([A-Za-z][0-9A-Za-z]*) event</a></li>'
    var state: enum {
        start,
        gt,
        gt_name,
        gt_name_sp,
        gt_name_sp_e,
        gt_name_sp_ev,
        gt_name_sp_eve,
        gt_name_sp_even,
        gt_name_sp_event,
        gt_name_sp_event_lt,
        gt_name_sp_event_lt_slash,
        gt_name_sp_event_lt_slash_a,
        gt_name_sp_event_lt_slash_a_gt,
        gt_name_sp_event_lt_slash_a_gt_lt,
        gt_name_sp_event_lt_slash_a_gt_lt_slash,
        gt_name_sp_event_lt_slash_a_gt_lt_slash_l,
        gt_name_sp_event_lt_slash_a_gt_lt_slash_li,
    } = .start;
    var name_start: usize = 0;
    var name_end: usize = 0;
    for (contents) |c, i| {
        switch (state) {
            .start => state = if (c == '>') .gt else .start,
            .gt => switch (c) {
                'A'...'Z', 'a'...'z' => {
                    state = .gt_name;
                    name_start = i;
                },
                else => {
                    state = .start;
                },
            },
            .gt_name => switch (c) {
                '0'...'9', 'A'...'Z', 'a'...'z' => {},
                ' ' => {
                    name_end = i;
                    state = .gt_name_sp;
                },
                else => {
                    state = .start;
                },
            },
            .gt_name_sp => state = if (c == 'e') .gt_name_sp_e else .start,
            .gt_name_sp_e => state = if (c == 'v') .gt_name_sp_ev else .start,
            .gt_name_sp_ev => state = if (c == 'e') .gt_name_sp_eve else .start,
            .gt_name_sp_eve => state = if (c == 'n') .gt_name_sp_even else .start,
            .gt_name_sp_even => state = if (c == 't') .gt_name_sp_event else .start,
            .gt_name_sp_event => state = if (c == '<') .gt_name_sp_event_lt else .start,
            .gt_name_sp_event_lt => state = if (c == '/') .gt_name_sp_event_lt_slash else .start,
            .gt_name_sp_event_lt_slash => state = if (c == 'a') .gt_name_sp_event_lt_slash_a else .start,
            .gt_name_sp_event_lt_slash_a => state = if (c == '>') .gt_name_sp_event_lt_slash_a_gt else .start,
            .gt_name_sp_event_lt_slash_a_gt => state = if (c == '<') .gt_name_sp_event_lt_slash_a_gt_lt else .start,
            .gt_name_sp_event_lt_slash_a_gt_lt => state = if (c == '/') .gt_name_sp_event_lt_slash_a_gt_lt_slash else .start,
            .gt_name_sp_event_lt_slash_a_gt_lt_slash => state = if (c == 'l') .gt_name_sp_event_lt_slash_a_gt_lt_slash_l else .start,
            .gt_name_sp_event_lt_slash_a_gt_lt_slash_l => state = if (c == 'i') .gt_name_sp_event_lt_slash_a_gt_lt_slash_li else .start,
            .gt_name_sp_event_lt_slash_a_gt_lt_slash_li => {
                if (c == '>') {
                    _ = try name_set.put(contents[name_start..name_end], {});
                }
                state = .start;
            },
        }
    }

    name_set.sort(struct {
        keys: [][]const u8,
        pub fn lessThan(ctx: @This(), a_index: usize, b_index: usize) bool {
            return std.mem.lessThan(u8, ctx.keys[a_index], ctx.keys[b_index]);
        }
    }{ .keys = name_set.keys() });

    try writeZigEnum("src/client/zig/_generated_EventType.zig", "EventType", name_set.keys());
    try writeJsEnum("src/client/js/_generated_EventType.js", name_set.keys());
}

fn writeZigEnum(path: []const u8, enum_name: []const u8, item_names: [][]const u8) !void {
    var file = try std.fs.cwd().createFile(path, .{});
    defer file.close();

    var buffered_writer = std.io.bufferedWriter(file.writer());
    var writer = buffered_writer.writer();

    try writer.print(
        \\// This file was generated by: zig run tools/generate-enums.zig
        \\pub const {s} = enum(i32) {{
        \\
    ,
        .{enum_name},
    );

    for (item_names) |name, i| {
        if (std.mem.eql(u8, name, "error") or
            std.mem.eql(u8, name, "suspend") or
            std.mem.eql(u8, name, "resume"))
        {
            try writer.print("    @\"{s}\" = {},\n", .{ name, i });
        } else {
            try writer.print("    {s} = {},\n", .{ name, i });
        }
    }
    try writer.writeAll("};\n");
    try buffered_writer.flush();
}

fn writeJsEnum(path: []const u8, item_names: [][]const u8) !void {
    var file = try std.fs.cwd().createFile(path, .{});
    defer file.close();

    var buffered_writer = std.io.bufferedWriter(file.writer());
    var writer = buffered_writer.writer();

    try writer.writeAll(
        \\// This file was generated by: zig run tools/generate-enums.zig
        \\return [
        \\
    );

    for (item_names) |name| {
        try writer.print("    \"{s}\",\n", .{name});
    }
    try writer.writeAll("];\n");
    try buffered_writer.flush();
}

fn downloadEntireUrl(url: []const u8) ![]u8 {
    const uri = try std.Uri.parse(url);
    var req = try http_client.request(uri, .{}, .{});
    defer req.deinit();
    // Use a buffered reader to work around a bug in the tls implementation.
    var br = std.io.bufferedReaderSize(std.crypto.tls.max_ciphertext_record_len, req.reader());
    return try br.reader().readAllAlloc(gpa, 10_000_000);
}
