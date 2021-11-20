const std = @import("std");
const Allocator = std.mem.Allocator;
const Groove = @import("groove.zig").Groove;
const SoundIo = @import("soundio.zig").SoundIo;

pub var groove: *Groove = undefined;
pub var soundio: *SoundIo = undefined;
pub var gpa: *Allocator = undefined;
