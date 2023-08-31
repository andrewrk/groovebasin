const std = @import("std");
const Allocator = std.mem.Allocator;
const log = std.log;
const ArrayList = std.ArrayList;
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const AutoArrayHashMapUnmanaged = std.AutoArrayHashMapUnmanaged;
const AutoArrayHashMap = std.AutoArrayHashMap;
const assert = std.debug.assert;
const Iovec = std.os.iovec;

const g = @import("global.zig");

const StringPool = @import("StringPool.zig");
const subscriptions = @import("subscriptions.zig");

const SubscriptionTag = std.meta.Tag(@import("groovebasin_protocol.zig").Subscription);
const SubscriptionBoolArray = [std.enums.directEnumArrayLen(SubscriptionTag, 0)]bool;
const subscription_bool_array_initial_value = std.enums.directEnumArrayDefault(SubscriptionTag, bool, false, 0, .{});

const all_databases = .{
    &@import("users.zig").sessions,
    &@import("users.zig").user_accounts,
};

const FileHeader = extern struct {
    /// This confirms that this is in fact a groovebasin db.
    magic_number: [4]u8 = .{ 0xf6, 0x2e, 0xb5, 0x9a },
    /// Panic with a todo if this isn't right.
    endian_check: u16 = 0x1234,
    /// Bump this during devlopment to signal a breaking change.
    /// This causes existing dbs on old versions to be silently *deleted*.
    dev_version: u16 = 6,
};

const database_index_size: usize = blk: {
    comptime var size: usize = 0;
    inline for (all_databases) |d| {
        if (@TypeOf(d.*).has_strings) {
            size += @sizeOf(u32);
        }
        size += @sizeOf(u32);
    }
    break :blk size;
};

pub fn load(db_path: []const u8) !void {
    var header_buf: [@sizeOf(FileHeader) + database_index_size]u8 = undefined;
    const file = std.fs.cwd().openFile(db_path, .{}) catch |err| switch (err) {
        error.FileNotFound => {
            log.warn("no db found. starting from scratch", .{});
            return;
        },
        else => |e| return e,
    };
    defer file.close();
    const len_upper_bound = try file.getEndPos();

    try file.reader().readNoEof(&header_buf);

    {
        const found_header = std.mem.bytesAsValue(FileHeader, header_buf[0..@sizeOf(FileHeader)]);
        if (!std.mem.eql(u8, &found_header.magic_number, &(FileHeader{}).magic_number)) return error.NotAGrooveBasinDb;
        if (found_header.endian_check != (FileHeader{}).endian_check) @panic("TODO: consider endianness");
        if (found_header.dev_version != (FileHeader{}).dev_version) {
            log.warn("found db from older dev version: {} (current: {}). deleting database.", .{
                found_header.dev_version,
                (FileHeader{}).dev_version,
            });
            return;
        }
    }

    var header_cursor: usize = @sizeOf(FileHeader);
    var iovec_array: [database_index_size / @sizeOf(u32) + all_databases.len]Iovec = undefined;
    var i: usize = 0;
    inline for (all_databases) |d| {
        if (@TypeOf(d.*).has_strings) {
            const len = try parseLen(&header_buf, &header_cursor, len_upper_bound);
            assert(d.strings.len() == 0);
            const slice = try d.strings.buf.addManyAsSlice(g.gpa, len);
            iovec_array[i] = .{
                .iov_base = slice.ptr,
                .iov_len = slice.len,
            };
            i += 1;
        }

        const len = try parseLen(&header_buf, &header_cursor, len_upper_bound);
        try d.table.entries.resize(g.gpa, len);
        {
            const slice = std.mem.sliceAsBytes(d.table.keys());
            iovec_array[i] = .{
                .iov_base = slice.ptr,
                .iov_len = slice.len,
            };
            i += 1;
        }
        {
            const slice = std.mem.sliceAsBytes(d.table.values());
            iovec_array[i] = .{
                .iov_base = slice.ptr,
                .iov_len = slice.len,
            };
            i += 1;
        }
    }
    assert(i == iovec_array.len);
}

fn parseLen(header_buf: *[@sizeOf(FileHeader) + database_index_size]u8, cursor: *usize, len_upper_bound: usize) !u32 {
    const len = std.mem.bytesToValue(u32, header_buf[cursor.*..][0..@sizeOf(u32)]);
    if (len > len_upper_bound) {
        return error.DataCorruption; // db specifies a len that exceeds the file size.
    }
    cursor.* += @sizeOf(u32);
    return @intCast(len);
}

pub fn save(db_path: []const u8) !void {
    _ = save; // TODO
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
        pub const has_strings = hasStrings(Value);

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
