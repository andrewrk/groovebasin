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
const users = @import("users.zig");
const subscriptions = @import("subscriptions.zig");
const Id = @import("groovebasin_protocol.zig").Id;

const FileHeader = extern struct {
    /// This confirms that this is in fact a groovebasin db.
    magic_number: [4]u8 = .{ 0xf6, 0x2e, 0xb5, 0x9a },
    /// Panic with a todo if this isn't right.
    endian_check: u16 = 0x1234,
    /// Bump this during devlopment to signal a breaking change.
    /// This causes existing dbs on old versions to be silently *deleted*.
    dev_version: u16 = 5,
};

const write_buffer_size = 0x100_000;
const DbBufferedWriter = std.io.BufferedWriter(write_buffer_size, std.fs.File.Writer);
var db_file: std.fs.File = undefined;
var db_buffered_writer: DbBufferedWriter = undefined;
var db_writer: DbBufferedWriter.Writer = undefined;
var len_upper_bound: usize = undefined;

pub fn load(db_path: []const u8) !void {
    db_file = try std.fs.cwd().createFile(db_path, .{
        .read = true,
        .truncate = false,
    });
    errdefer db_file.close();

    if ((try db_file.stat()).size < @sizeOf(FileHeader)) {
        log.warn("starting with an empty database: {s}", .{db_path});
        return createEmptyDatabase();
    }

    {
        const found_header = try db_file.reader().readStruct(FileHeader);
        if (!std.mem.eql(u8, &found_header.magic_number, &(FileHeader{}).magic_number)) return error.NotAGrooveBasinDb;
        if (found_header.endian_check != (FileHeader{}).endian_check) @panic("TODO: consider endianness");
        if (found_header.dev_version != (FileHeader{}).dev_version) {
            log.warn("found db from older dev version: {} (current: {}). deleting database.", .{
                found_header.dev_version,
                (FileHeader{}).dev_version,
            });
            return createEmptyDatabase();
        }
    }
    len_upper_bound = try db_file.getEndPos() + write_buffer_size;

    var arena = std.heap.ArenaAllocator.init(g.gpa);
    defer arena.deinit();
    var entry_counter: usize = 0;
    while (true) {
        _ = arena.reset(.retain_capacity);
        const entry_start_pos = try db_file.getPos();
        readEntry(arena.allocator(), db_file.reader()) catch |err| switch (err) {
            error.EndOfStream => {
                if (entry_start_pos == try db_file.getPos()) break; // done
                log.warn("Ignoring truncated final entry after reading {} db entries successfully", .{entry_counter});
                try db_file.seekTo(entry_start_pos);
                try db_file.setEndPos(entry_start_pos);
                return beginWriteMode();
            },
            else => {
                // This is not supposed to happen. No obvious way to recover from this.
                log.err("Error encountered after reading {} db entries successfully: {s}", .{ entry_counter, @errorName(err) });
                @panic("Cannot recover from db read error. Please retry or manually delete/move the db.");
            },
        };
        entry_counter += 1;
    }
    log.info("Loaded {} db entries", .{entry_counter});
    return beginWriteMode();
}

pub fn createEmptyDatabase() !void {
    // Start over and truncate.
    try db_file.seekTo(0);
    try db_file.setEndPos(0);
    try beginWriteMode();
    // Write header.
    try db_writer.writeStruct(FileHeader{});
    try db_buffered_writer.flush();
}

pub fn beginWriteMode() !void {
    db_buffered_writer = .{ .unbuffered_writer = db_file.writer() };
    db_writer = db_buffered_writer.writer();
}

const ChangeTag = enum(u8) {
    users_strings,
    user_accounts,
    // guest_perms,

    _, // getting an unnamed value means the db is corrupted.
};

const SubscriptionTag = std.meta.Tag(@import("groovebasin_protocol.zig").Subscription);
const SubscriptionBoolArray = [std.enums.directEnumArrayLen(SubscriptionTag, 0)]bool;
const subscription_bool_array_initial_value = std.enums.directEnumArrayDefault(SubscriptionTag, bool, false, 0, .{});

pub const Changes = struct {
    arena: Allocator,

    subscriptions_to_broadcast: SubscriptionBoolArray = subscription_bool_array_initial_value,

    // Changes for subsystems
    users_strings_start_len: u32,
    user_accounts: AutoArrayHashMap(Id, void),

    pub fn init(arena: Allocator) @This() {
        return .{
            .arena = arena,
            .users_strings_start_len = users.strings.len(),
            .user_accounts = AutoArrayHashMap(Id, void).init(arena),
        };
    }
    pub fn deinit(self: *@This()) void {
        self.user_accounts.deinit();
        self.* = undefined;
    }

    pub fn broadcastChanges(self: *@This(), name: SubscriptionTag) void {
        self.subscriptions_to_broadcast[@intFromEnum(name)] = true;
    }

    pub fn flush(self: *@This()) !void {
        try self.writeToDisk();
        try self.sendToClients();
    }

    fn writeToDisk(self: *@This()) !void {
        try writeStringPool(db_writer, .users_strings, &users.strings, self.users_strings_start_len);
        try writeHashMap(self.arena, db_writer, .user_accounts, &users.user_accounts, self.user_accounts.keys());

        try db_buffered_writer.flush();
    }

    fn sendToClients(self: *@This()) error{OutOfMemory}!void {
        for (self.subscriptions_to_broadcast, 0..) |should_broadcast, i| {
            if (!should_broadcast) continue;
            const name: SubscriptionTag = @enumFromInt(i);
            try subscriptions.broadcastChanges(self.arena, name);
        }
    }
};

fn readEntry(arena: Allocator, reader: anytype) !void {
    const tag: ChangeTag = @enumFromInt(try reader.readByte());
    switch (tag) {
        .users_strings => try readStringPool(reader, &users.strings),
        .user_accounts => try readHashMap(arena, reader, &users.user_accounts),
        else => {
            log.err("unrecognized entry tag: {}", .{tag});
            return error.UnrecognizedTag;
        },
    }
}

fn writeStringPool(writer: anytype, tag: ChangeTag, strings: *StringPool, start_offset: u32) !void {
    const len = strings.len() - start_offset;
    if (len == 0) return;
    try writer.writeByte(@intFromEnum(tag));
    try writer.writeIntNative(u32, len);
    try writer.writeAll(strings.buf.items[start_offset..]);
}

fn readStringPool(reader: anytype, strings: *StringPool) !void {
    const len = try readLenBounded(reader, 1);
    if (len == 0) return error.DataCorruption; // 0-len string pool.
    try reader.readNoEof(try strings.buf.addManyAsSlice(len));
    if (strings.buf.items[strings.len() - 1] != 0) return error.DataCorruption; // strings aren't null terminated.
}

fn writeHashMap(arena: Allocator, writer: anytype, tag: ChangeTag, hash_map: anytype, changed_keys: anytype) !void {
    if (changed_keys.len == 0) return;

    const key_size = @sizeOf(KeyOfHashMap(@TypeOf(hash_map.*)));

    var deleted_keys: ArrayListUnmanaged(u8) = .{};
    var put_keys: ArrayListUnmanaged(u8) = .{};
    // Pessimistically preallocate all we could need.
    try deleted_keys.ensureTotalCapacity(arena, key_size * changed_keys.len);
    try put_keys.ensureTotalCapacity(arena, key_size * changed_keys.len);
    for (changed_keys) |key| {
        if (hash_map.contains(key)) {
            put_keys.appendSliceAssumeCapacity(std.mem.asBytes(&key));
        } else {
            deleted_keys.appendSliceAssumeCapacity(std.mem.asBytes(&key));
        }
    }

    try writer.writeByte(@intFromEnum(tag));
    try writer.writeIntNative(u32, @intCast(deleted_keys.items.len / key_size));
    try writer.writeIntNative(u32, @intCast(put_keys.items.len / key_size));

    try writer.writeAll(std.mem.sliceAsBytes(deleted_keys.items));

    try writer.writeAll(std.mem.sliceAsBytes(put_keys.items));

    for (changed_keys) |key| {
        if (hash_map.contains(key)) {
            try writer.writeAll(std.mem.asBytes(&hash_map.get(key).?));
        }
    }
}

fn readHashMap(arena: Allocator, reader: anytype, hash_map: anytype) !void {
    const Key = KeyOfHashMap(@TypeOf(hash_map.*));
    const key_size = @sizeOf(Key);
    const Value = ValueOfHashMap(@TypeOf(hash_map.*));
    const value_size = @sizeOf(Value);

    const delete_count = try readLenBounded(reader, key_size);
    const put_count = try readLenBounded(reader, key_size + value_size);
    if (delete_count + put_count == 0) return error.DataCorruption; // 0 hash map modifications

    const buf = try arena.alloc(u8, key_size * delete_count + (key_size + value_size) * put_count);
    try reader.readNoEof(buf);

    for (0..delete_count) |i| {
        _ = hash_map.swapRemove(std.mem.bytesToValue(Key, buf[key_size * i ..][0..key_size]));
    }

    try hash_map.ensureUnusedCapacity(put_count);
    const put_keys_start = key_size * delete_count;
    const put_values_start = put_keys_start + key_size * put_count;
    for (0..put_count) |i| {
        const key = std.mem.bytesToValue(Key, buf[put_keys_start + key_size * i ..][0..key_size]);
        const value = std.mem.bytesToValue(Value, buf[put_values_start + value_size * i ..][0..value_size]);
        _ = hash_map.putAssumeCapacity(key, value);
    }
}

fn readLenBounded(reader: anytype, comptime item_size: comptime_int) !u32 {
    const upper_bound = (len_upper_bound + item_size - 1) / item_size;
    const value = try reader.readIntNative(u32);
    if (value > upper_bound) {
        return error.DataCorruption; // db specifies a len that far exceeds file size.
    }
    return value;
}

fn KeyOfHashMap(comptime HM: type) type {
    // It would be nice if this was just HM.Key
    return @typeInfo(@TypeOf(HM.get)).Fn.params[1].type.?;
}
fn ValueOfHashMap(comptime HM: type) type {
    return @typeInfo(@typeInfo(@TypeOf(HM.get)).Fn.return_type.?).Optional.child;
}
