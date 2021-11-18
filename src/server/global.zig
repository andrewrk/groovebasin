const std = @import("std");
const Allocator = std.mem.Allocator;
const Groove = @import("groove.zig").Groove;

pub var groove: *Groove = undefined;
pub var gpa: *Allocator = undefined;
