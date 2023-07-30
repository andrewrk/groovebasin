const std = @import("std");
const Mutex = std.Thread.Mutex;
const Condition = std.Thread.Condition;

pub fn channel(fifo: anytype) Channel(@TypeOf(fifo)) {
    return Channel(@TypeOf(fifo)).init(fifo);
}

pub fn Channel(comptime FifoType: type) type {
    return struct {
        fifo: FifoType,
        mutex: Mutex = .{},
        cond: Condition = .{},

        const T = @typeInfo(@TypeOf(FifoType.writeItem)).Fn.params[1].type.?;

        pub fn init(fifo: FifoType) @This() {
            return .{ .fifo = fifo };
        }
        pub fn deinit(self: *@This()) void {
            self.fifo.deinit();
            self.* = undefined;
        }

        pub fn put(self: *@This(), item: T) error{OutOfMemory}!void {
            self.mutex.lock();
            defer self.mutex.unlock();
            try self.fifo.writeItem(item);
            self.cond.signal();
        }

        pub fn getBlocking(self: *@This()) T {
            self.mutex.lock();
            defer self.mutex.unlock();
            while (true) {
                return self.fifo.readItem() orelse {
                    self.cond.wait(&self.mutex);
                    continue;
                };
            }
        }

        pub fn get(self: *@This()) ?T {
            self.mutex.lock();
            defer self.mutex.unlock();
            return self.fifo.readItem();
        }
    };
}
