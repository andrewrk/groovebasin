const Atomic = @import("std").atomic.Atomic;

count: Atomic(usize) = .{ .value = 0 },

pub fn init(initial_value: usize) @This() {
    return .{ .value = initial_value };
}

pub fn ref(self: *@This()) void {
    _ = self.count.fetchAdd(1, .Monotonic); // no ordering necessary, just updating a counter
}

/// Returns true iff the count reached zero.
pub fn unref(self: *@This()) bool {
    // Release ensures code before unref() happens-before the count is decremented and checked.
    if (self.count.fetchSub(1, .Release) == 0) {
        // Acquire ensures count decrement and code before previous unrefs()s happens-before we return.
        self.count.fence(.Acquire);
        return true;
    }
    return false;
}
