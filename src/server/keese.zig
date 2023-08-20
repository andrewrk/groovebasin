const std = @import("std");
const Allocator = std.mem.Allocator;
const Order = std.math.Order;
const json = std.json;

const StringPool = @import("shared").StringPool;

var strings: StringPool = undefined;

pub fn init(allocator: Allocator) !void {
    strings = StringPool.init(allocator);
}

pub fn deinit() void {
    strings.deinit();
}

pub const Value = union(enum) {
    fixed: struct {
        int: u16,
        frac: u16,
    },
    dynamic: u32, // offset into the keese string pool.

    // JSON interface
    pub fn jsonParse(allocator: Allocator, source: anytype, options: json.ParseOptions) !@This() {
        _ = options;
        switch (try source.nextAlloc(.alloc_if_needed)) {
            .string => |s| return parse(s),
            .allocated_string => |s| {
                defer allocator.free(s);
                return parse(s) catch return error.UnexpectedToken;
            },
            else => return error.UnexpectedToken,
        }
    }
    pub fn jsonParseFromValue(allocator: Allocator, source: json.Value, options: json.ParseOptions) !@This() {
        _ = allocator;
        _ = options;
        switch (source) {
            .string => |s| return parse(s) catch return error.UnexpectedToken,
            else => return error.UnexpectedToken,
        }
    }
    pub fn jsonStringify(self: @This(), jw: anytype) !void {
        switch (self) {
            .fixed => |f| {
                var buf: [8]u8 = undefined;
                try jw.write(writeFixed(&buf, f.int, f.frac));
            },
            .dynamic => |offset| {
                try jw.write(strings.getString(offset));
            },
        }
    }

    // std.fmt interface
    pub fn format(self: @This(), comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;
        switch (self) {
            .fixed => |f| {
                var buf: [8]u8 = undefined;
                return writer.writeAll(writeFixed(&buf, f.int, f.frac));
            },
            .dynamic => |offset| {
                return writer.writeAll(strings.getString(offset));
            },
        }
    }
};

pub fn parse(s: []const u8) !Value {
    const magnitude = blk: for (s, 0..) |c, i| {
        if (c == '~') continue;
        break :blk i;
    } else return error.MalformedKeeseValue; // No digits.
    const implicit_decimal_point = magnitude + magnitude + 1;

    if (implicit_decimal_point > s.len) return error.MalformedKeeseValue; // More magnitude than digits.
    const int_part = s[magnitude..implicit_decimal_point];
    const frac_part = s[implicit_decimal_point..];

    var int_value: u16 = 0;
    for (int_part) |c| {
        if (int_value >= (1 << 10)) return dynamicFallback(s, magnitude);
        int_value <<= 6;
        int_value |= try getCharValue(c);
    }

    var frac_value: u16 = 0;
    switch (frac_part.len) {
        0 => {},
        1 => {
            frac_value |= (try getCharValue(frac_part[0])) << 10;
        },
        2 => {
            frac_value |= (try getCharValue(frac_part[0])) << 10;
            frac_value |= (try getCharValue(frac_part[1])) << 4;
        },
        3 => {
            frac_value |= (try getCharValue(frac_part[0])) << 10;
            frac_value |= (try getCharValue(frac_part[1])) << 4;
            const last_v = (try getCharValue(frac_part[2]));
            if (last_v & 0x3 != 0) return dynamicFallback(s, magnitude);
            frac_value |= last_v >> 2;
        },
        else => return dynamicFallback(s, magnitude),
    }

    return .{ .fixed = .{ .int = int_value, .frac = frac_value } };
}

fn dynamicFallback(s: []const u8, magnitude: usize) !Value {
    // Magnitude has already been validated at this point.
    // Just check the digits.
    for (s[magnitude..]) |c| {
        _ = try getCharValue(c);
    }
    return .{ .dynamic = try strings.putWithoutDeduplication(s) };
}

fn writeFixed(out_buf: *[8]u8, int_value: u16, frac_value: u16) []const u8 {
    var implicit_decimal_point: usize = 0;
    if (int_value >= (1 << 12)) {
        out_buf[0] = '~';
        out_buf[1] = '~';
        out_buf[2] = alphabet[int_value >> 12];
        out_buf[3] = alphabet[(int_value >> 6) & 0x3f];
        out_buf[4] = alphabet[int_value & 0x3f];
        implicit_decimal_point = 5;
    } else if (int_value >= (1 << 6)) {
        out_buf[0] = '~';
        out_buf[1] = alphabet[(int_value >> 6) & 0x3f];
        out_buf[2] = alphabet[int_value & 0x3f];
        implicit_decimal_point = 3;
    } else {
        out_buf[0] = alphabet[int_value];
        implicit_decimal_point = 1;
    }
    if (frac_value & 0xf != 0) {
        out_buf[implicit_decimal_point] = alphabet[frac_value >> 10];
        out_buf[implicit_decimal_point + 1] = alphabet[(frac_value >> 4) & 0x3f];
        out_buf[implicit_decimal_point + 2] = alphabet[(frac_value << 2) & 0x3f];
        return out_buf[0 .. implicit_decimal_point + 3];
    } else if (frac_value & 0x3ff != 0) {
        out_buf[implicit_decimal_point] = alphabet[frac_value >> 10];
        out_buf[implicit_decimal_point + 1] = alphabet[(frac_value >> 4) & 0x3f];
        return out_buf[0 .. implicit_decimal_point + 2];
    } else if (frac_value & 0xffff != 0) {
        out_buf[implicit_decimal_point] = alphabet[frac_value >> 10];
        return out_buf[0 .. implicit_decimal_point + 1];
    } else {
        return out_buf[0..implicit_decimal_point];
    }
}

const alphabet = "0123456789?@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
comptime {
    std.debug.assert(alphabet.len == 64);
}
const alphabet_lookup_table: [256]i7 = blk: {
    var result = [1]i7{-1} ** 256;
    for (alphabet, 0..) |c, i| {
        result[c] = @intCast(i);
    }
    break :blk result;
};

fn getCharValue(c: u8) !u16 {
    var v = alphabet_lookup_table[c];
    if (v < 0) return error.MalformedKeeseValue; // Unrecognized digit.
    return @intCast(v);
}

pub fn order(a: Value, b: Value) Order {
    if (a == .fixed and b == .fixed) {
        const a_u32 = (@as(u32, a.fixed.int) << 16) | a.fixed.frac;
        const b_u32 = (@as(u32, b.fixed.int) << 16) | b.fixed.frac;
        return std.math.order(a_u32, b_u32);
    }
    if (a == .dynamic and b == .dynamic) {
        const a_str = strings.getString(a.dynamic);
        const b_str = strings.getString(b.dynamic);
        return std.mem.order(u8, a_str, b_str);
    }
    if (a == .fixed and b == .dynamic) {
        var buf: [8]u8 = undefined; // "~~122UUU"
        const b_str = strings.getString(b.dynamic);
        return std.mem.order(u8, writeFixed(&buf, a.fixed.int, a.fixed.frac), b_str);
    }
    if (a == .dynamic and b == .fixed) {
        const a_str = strings.getString(a.dynamic);
        var buf: [8]u8 = undefined; // "~~122UUU"
        return std.mem.order(u8, a_str, writeFixed(&buf, b.fixed.int, b.fixed.frac));
    }
    unreachable;
}

pub fn between(low: ?Value, high: ?Value) Value {
    _ = low;
    _ = high;
    @panic("TODO: do we need to generate keese values server side?");
}

const testing = std.testing;

test "keese parse" {
    try init(testing.allocator);
    defer deinit();

    _ = try parse("1");
    _ = try parse("~10");
    _ = try parse("~~~1000");
    _ = try parse("1F");
    _ = try parse("1FFFF");
    _ = try parse("~~~1000F");
    _ = try parse("~~~1000FFFF");

    // Unrecognized digit.
    try testing.expectError(error.MalformedKeeseValue, parse(" "));
    try testing.expectError(error.MalformedKeeseValue, parse("1%"));
    try testing.expectError(error.MalformedKeeseValue, parse("~~~~10000|"));

    // No digits / More magnitude than digits.
    try testing.expectError(error.MalformedKeeseValue, parse(""));
    try testing.expectError(error.MalformedKeeseValue, parse("~"));
    try testing.expectError(error.MalformedKeeseValue, parse("~1"));
    try testing.expectError(error.MalformedKeeseValue, parse("~~~~1000"));
}

test "keese writeFixed" {
    try init(testing.allocator);
    defer deinit();

    // int
    try testWriteFixed("1");
    try testWriteFixed("z");
    try testWriteFixed("~10");
    try testWriteFixed("~zz");
    try testWriteFixed("~~100");
    try testWriteFixed("~~Dzz");

    // frac
    try testWriteFixed("1z");
    try testWriteFixed("1zz");
    try testWriteFixed("1zz4");

    // int and frac
    try testWriteFixed("~zzz");
    try testWriteFixed("~zzzz");
    try testWriteFixed("~zzzz4");
    try testWriteFixed("~~Dzzz");
    try testWriteFixed("~~Dzzzz");
    try testWriteFixed("~~Dzzzz4");
}

fn testWriteFixed(s: []const u8) !void {
    const value = try parse(s);
    var buf: [8]u8 = undefined;
    const restringified = writeFixed(&buf, value.fixed.int, value.fixed.frac);
    try testing.expectEqualStrings(s, restringified);
}

test "keese order" {
    try init(testing.allocator);
    defer deinit();

    // fixed
    try testing.expectEqual(Order.lt, order(try parse("1"), try parse("2")));
    try testing.expectEqual(Order.eq, order(try parse("2"), try parse("2")));
    try testing.expectEqual(Order.gt, order(try parse("3"), try parse("2")));
    try testing.expectEqual(Order.lt, order(try parse("1"), try parse("1U")));
    try testing.expectEqual(Order.gt, order(try parse("2"), try parse("1U")));
    try testing.expectEqual(Order.gt, order(try parse("~10"), try parse("1U")));

    // fixed + dynamic
    try testing.expectEqual(Order.lt, order(try parse("z"), try parse("~~~1000")));
    try testing.expectEqual(Order.gt, order(try parse("~~~1000"), try parse("z")));

    // dnymaic + dynamic
    try testing.expectEqual(Order.lt, order(try parse("~~~1000"), try parse("~~~1000U")));
    try testing.expectEqual(Order.lt, order(try parse("9zzzz"), try parse("~~~1000")));
}
