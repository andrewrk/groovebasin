//! This file contains lists of enum(i32) types that match browser strings used in various contexts.
//! Since there's a finite set of strings, it's more natural to pass i32 back and forth rather than variable-sized strings.
//!
//! Some enums are scraped from a webpage as noted below.
//! This formatter command can be used to deduplicate, sort, and number enum names from stdin:
//!     python3 -c 'import sys; print("\n".join("    {} = {},".format(name, i) for i, name in enumerate(sorted(set(sys.stdin.read().split())))))'
//! Note that any Zig keywords must be @"quoted" afterward.

pub const InsertPosition = enum(i32) {
    beforebegin = 0,
    afterbegin = 1,
    beforeend = 2,
    afterend = 3,
};

/// bitfield of (1 << EventModifierKey._)
pub const EventModifiers = i32;
pub const EventModifierKey = enum(i32) {
    shift = 0,
    ctrl = 1,
    alt = 2,
    meta = 3,
};
pub fn getModifier(modifiers: EventModifiers, modifier: EventModifierKey) bool {
    return modifiers & (@as(i32, 1) << @intCast(u5, @enumToInt(modifier))) != 0;
}

pub const EventType = @import("./_generated_EventType.zig").EventType;

pub const KeyboardEventCode = @import("./_generated_KeyboardEventCode.zig").KeyboardEventCode;
