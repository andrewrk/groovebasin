const std = @import("std");
const Allocator = std.mem.Allocator;

var gpa_state: std.heap.GeneralPurposeAllocator(.{}) = .{};
pub const gpa = &gpa_state.allocator;