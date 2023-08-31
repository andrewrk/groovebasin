const std = @import("std");
const Allocator = std.mem.Allocator;
const log = std.log;
const ArrayList = std.ArrayList;
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const AutoArrayHashMapUnmanaged = std.AutoArrayHashMapUnmanaged;
const AutoArrayHashMap = std.AutoArrayHashMap;
const assert = std.debug.assert;

const g = @import("global.zig");

const StringPool = @import("StringPool.zig");
const subscriptions = @import("subscriptions.zig");

const SubscriptionTag = std.meta.Tag(@import("groovebasin_protocol.zig").Subscription);
const SubscriptionBoolArray = [std.enums.directEnumArrayLen(SubscriptionTag, 0)]bool;
const subscription_bool_array_initial_value = std.enums.directEnumArrayDefault(SubscriptionTag, bool, false, 0, .{});

pub fn load(db_path: []const u8) !void {
    _ = db_path; // TODO
}

pub const Changes = struct {
    subscriptions_to_broadcast: SubscriptionBoolArray = subscription_bool_array_initial_value,

    pub fn broadcastChanges(self: *@This(), name: SubscriptionTag) void {
        self.subscriptions_to_broadcast[@intFromEnum(name)] = true;
    }

    pub fn sendToClients(self: *@This(), arena: Allocator) error{OutOfMemory}!void {
        for (self.subscriptions_to_broadcast, 0..) |should_broadcast, i| {
            if (!should_broadcast) continue;
            const name: SubscriptionTag = @enumFromInt(i);
            try subscriptions.broadcastChanges(arena, name);
        }
    }
};

//===== new API v3 for real this time =====

pub fn Database(comptime Key: type, comptime Value: type, comptime name: SubscriptionTag) type {
    return struct {
        const has_strings = hasStrings(Value);

        allocator: Allocator,
        table: AutoArrayHashMapUnmanaged(Key, Value) = .{},
        strings: if (has_strings) StringPool else void = if (has_strings) .{} else {},

        pub fn init(allocator: Allocator) @This() {
            return .{
                .allocator = allocator,
            };
        }
        pub fn deinit(self: *@This()) void {
            self.table.deinit(self.allocator);
            if (has_strings) self.strings.deinit(self.allocator);
            self.* = undefined;
        }

        pub fn get(self: @This(), key: Key) *const Value {
            return self.table.getEntry(key).?.value_ptr;
        }
        pub fn getForEditing(self: *@This(), changes: *Changes, key: Key) !*Value {
            changes.broadcastChanges(name);
            return self.table.getEntry(key).?.value_ptr;
        }
        pub fn remove(self: *@This(), changes: *Changes, key: Key) void {
            changes.broadcastChanges(name);
            assert(self.table.swapRemove(key));
        }
        pub fn putNoClobber(self: *@This(), changes: *Changes, key: Key, value: Value) !void {
            changes.broadcastChanges(name);
            try self.table.putNoClobber(self.allocator, key, value);
        }
        pub fn contains(self: @This(), key: Key) bool {
            return self.table.contains(key);
        }

        pub fn iterator(self: *@This()) Iterator {
            return .{
                .sub_it = self.table.iterator(),
            };
        }

        pub const EntryConst = struct {
            key_ptr: *const Key,
            value_ptr: *const Value,
        };
        pub const Entry = AutoArrayHashMapUnmanaged(Key, Value).Entry;
        pub const Iterator = struct {
            sub_it: AutoArrayHashMapUnmanaged(Key, Value).Iterator,
            current_kv: ?Entry = null,

            pub fn next(self: *@This()) ?EntryConst {
                self.current_kv = self.sub_it.next();
                if (self.current_kv) |kv| {
                    return .{
                        .key_ptr = kv.key_ptr,
                        .value_ptr = kv.value_ptr,
                    };
                } else return null;
            }
            pub fn promoteForEditing(self: @This(), changes: *Changes, kv: EntryConst) Entry {
                assert(kv.key_ptr == self.current_kv.?.key_ptr);
                changes.broadcastChanges(name);
                return self.current_kv.?;
            }
        };
    };
}

fn hasStrings(comptime T: type) bool {
    for (@typeInfo(T).Struct.fields) |field| {
        if (field.type == StringPool.Index) return true;
        if (field.type == StringPool.OptionalIndex) return true;
    }
    return false;
}
