///! Dearest Josh,
///
///! Please stop using global variables.
///
///! Love, Andy
const std = @import("std");
const Allocator = std.mem.Allocator;
const Groove = @import("groove.zig").Groove;
const SoundIo = @import("soundio.zig").SoundIo;
const Player = @import("Player.zig");
const StringPool = @import("StringPool.zig");
const TheDatabase = @import("db.zig").TheDatabase;

pub var groove: *Groove = undefined;
pub var player: Player = undefined;
pub var soundio: *SoundIo = undefined;
pub var gpa: Allocator = undefined;
pub var strings: StringPool = .{};
pub var the_database: TheDatabase = .{};
pub var queue: *@import("queue.zig") = undefined;
