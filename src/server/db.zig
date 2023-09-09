const std = @import("std");
const Allocator = std.mem.Allocator;
const log = std.log;
const ArrayList = std.ArrayList;
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const AutoArrayHashMapUnmanaged = std.AutoArrayHashMapUnmanaged;
const AutoArrayHashMap = std.AutoArrayHashMap;
const assert = std.debug.assert;
const Iovec = std.os.iovec;
const IovecConst = std.os.iovec_const;

const g = @import("global.zig");

const StringPool = @import("StringPool.zig");
const subscriptions = @import("subscriptions.zig");

const protocol = @import("groovebasin_protocol.zig");
const Id = protocol.Id;
const ScanStatus = protocol.ScanStatus;
const Datetime = protocol.Datetime;
const EventUserId = protocol.EventUserId;
const RepeatState = protocol.RepeatState;

const SubscriptionTag = std.meta.Tag(protocol.Subscription);
const SubscriptionBoolArray = [std.enums.directEnumArrayLen(SubscriptionTag, 0)]bool;
const subscription_bool_array_initial_value = std.enums.directEnumArrayDefault(SubscriptionTag, bool, false, 0, .{});

// ===================

pub const UserAccount = struct {
    /// Display name and login username.
    username: StringPool.Index,
    password_hash: PasswordHash,
    registration_stage: RegistrationStage,
    permissions: InternalPermissions,
};

pub const RegistrationStage = enum(u3) {
    /// Freshly generated.
    guest_without_password = 0,
    /// Clients can (and do) set a random password automatically for guest accounts.
    guest_with_password = 1,
    /// If the user ever changes their name, they get here. This is required to advance.
    named_by_user = 2,
    /// Makes the user show up in the admin approval view.
    requested_approval = 3,
    /// Finally the user can have non-guest permissions.
    approved = 4,
};
pub const InternalPermissions = packed struct {
    read: bool,
    add: bool,
    control: bool,
    playlist: bool,
    admin: bool,
};

/// Uses sha256
pub const PasswordHash = struct {
    salt: [16]u8,
    hash: [32]u8,
};

pub const InternalSession = struct {
    user_id: Id,
    claims_to_be_streaming: bool = false,
};

pub const Track = struct {
    duration: f64,
    file_path: StringPool.Index,
    title: StringPool.Index,
    artist: StringPool.OptionalIndex,
    composer: StringPool.OptionalIndex,
    performer: StringPool.OptionalIndex,
    album_artist: StringPool.OptionalIndex,
    album: StringPool.OptionalIndex,
    genre: StringPool.OptionalIndex,
    track_number: i16,
    track_count: i16,
    disc_number: i16,
    disc_count: i16,
    year: i16,
    fingerprint_scan_status: ScanStatus = .not_started,
    loudness_scan_status: ScanStatus = .not_started,
    compilation: bool,
};

pub const Item = struct {
    sort_key: f64,
    track_key: Id,
    is_random: bool,
};

pub const InternalEvent = struct {
    date: Datetime,
    // TODO: we really don't need this. just sort on date.
    sort_key: f64,
    who: EventUserId,
    type: union(enum) {
        chat: struct {
            text: StringPool.Index,
            is_slash_me: bool,
        },
    },
};

pub const State = struct {
    current_item: ?struct {
        id: Id,
        state: union(enum) {
            /// track start date in milliseconds relative to now
            playing: i64,
            /// seconds into the song where the seek head is paused
            paused: f64,
        },
    } = null,
    auto_dj: packed struct {
        on: bool = false,
        history_size: u10 = 10,
        future_size: u10 = 10,
    } = .{},
    repeat: RepeatState = .off,
    volume_percent: u8 = 100, // 0%..200%
    hardware_playback: bool = false,
    guest_permissions: InternalPermissions = .{
        .read = true,
        .add = false,
        .control = true,
        .playlist = false,
        .admin = false,
    },
};

pub const TheDatabase = struct {
    sessions: Database(Id, InternalSession, .sessions, false) = .{},
    user_accounts: Database(Id, UserAccount, .users, true) = .{},
    tracks: Database(Id, Track, .library, true) = .{},
    items: Database(Id, Item, .queue, true) = .{},
    events: Database(Id, InternalEvent, .events, true) = .{},

    state: State = .{},

    state_version: Id = .{ .value = 0 },
    previous_state_version: Id = .{ .value = 0 },
    previous_state: State = .{},

    pub fn deinit(self: *@This()) void {
        self.sessions.deinit();
        self.user_accounts.deinit();
        self.tracks.deinit();
        self.items.deinit();
        self.events.deinit();
    }

    pub fn getState(self: *const @This()) *const State {
        return &self.state;
    }
    pub fn getStateForEditing(self: *@This()) *State {
        return &self.state;
    }
};

pub fn init() !void {
    inline for (all_databases) |d| {
        d.version = Id.random(); // TODO: load this from disk.
    }
}
pub fn deinit() void {
    g.the_database.deinit();
}

const all_databases = .{
    &g.the_database.user_accounts,
    &g.the_database.sessions,
    &g.the_database.tracks,
    &g.the_database.items,
    &g.the_database.events,
};

const FileHeader = extern struct {
    /// This confirms that this is in fact a groovebasin db.
    magic_number: [4]u8 = .{ 0xf6, 0x2e, 0xb5, 0x9a },
    /// Panic with a todo if this isn't right.
    endian_check: u16 = 0x1234,
    /// Bump this during devlopment to signal a breaking change.
    /// This causes existing dbs on old versions to be silently *deleted*.
    dev_version: u16 = 24,
};

const some_facts = blk: {
    comptime var fixed_size_header_size: usize = 0;
    comptime var number_of_dynamically_sized_sections: usize = 0;
    fixed_size_header_size += @sizeOf(FileHeader); // header
    fixed_size_header_size += @sizeOf(State); // fixed-size data
    fixed_size_header_size += @sizeOf(u32); // strings len
    number_of_dynamically_sized_sections += 1; // strings data
    inline for (all_databases) |d| {
        if (!@TypeOf(d.*).should_save_to_disk) continue;
        fixed_size_header_size += @sizeOf(u32); // number of items
        number_of_dynamically_sized_sections += 2; // keys, values
    }
    break :blk .{
        .fixed_size_header_size = fixed_size_header_size,
        .number_of_dynamically_sized_sections = number_of_dynamically_sized_sections,
    };
};

var db_path: []const u8 = undefined;
var db_path_tmp: []const u8 = undefined;

pub fn load(path: []const u8) !void {
    db_path = path;
    db_path_tmp = try std.mem.concat(g.gpa, u8, &.{ db_path, ".tmp" });

    var header_buf: [some_facts.fixed_size_header_size]u8 = undefined;
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
    g.the_database.state = std.mem.bytesToValue(State, header_buf[header_cursor..][0..@sizeOf(State)]);
    header_cursor += @sizeOf(State);

    var iovec_array: [some_facts.number_of_dynamically_sized_sections]Iovec = undefined;
    var i: usize = 0;
    {
        const len = try parseLen(&header_buf, &header_cursor, len_upper_bound);
        g.strings.clearRetainingCapacity();
        const slice = try g.strings.buf.addManyAsSlice(g.gpa, len);
        iovec_array[i] = .{
            .iov_base = slice.ptr,
            .iov_len = slice.len,
        };
        i += 1;
    }
    inline for (all_databases) |d| {
        if (!@TypeOf(d.*).should_save_to_disk) continue;

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

    // Big load.
    try readvNoEof(file, &iovec_array);

    // Handle loaded.
    try g.strings.reIndex(g.gpa);
    try g.strings.ensureConstants(g.gpa);
    inline for (all_databases) |d| {
        if (!@TypeOf(d.*).should_save_to_disk) continue;
        try d.table.reIndex(g.gpa);
        try checkForCorruption(&d.table);
    }

    log.info("loaded db bytes: {}", .{some_facts.fixed_size_header_size + totalIovecLen(&iovec_array)});
}

fn readvNoEof(file: std.fs.File, iovecs: []Iovec) !void {
    var expected_len = totalIovecLen(iovecs);
    const found_len = try file.readvAll(iovecs);
    if (found_len < expected_len) return error.EndOfStream;
    assert(found_len == expected_len);
}

fn totalIovecLen(iovecs: anytype) usize {
    var total: usize = 0;
    for (iovecs) |iovec| {
        total += iovec.iov_len;
    }
    return total;
}

fn parseLen(header_buf: *[some_facts.fixed_size_header_size]u8, cursor: *usize, len_upper_bound: usize) !u32 {
    const len = std.mem.bytesToValue(u32, header_buf[cursor.*..][0..@sizeOf(u32)]);
    if (len > len_upper_bound) {
        return error.DataCorruption; // db specifies a len that exceeds the file size.
    }
    cursor.* += @sizeOf(u32);
    return @intCast(len);
}

pub fn save() !void {
    try saveTo(db_path_tmp);
    try std.fs.cwd().rename(db_path_tmp, db_path);
}
fn saveTo(path: []const u8) !void {
    const file = try std.fs.cwd().createFile(path, .{});
    defer file.close();

    var header_cursor: usize = 0;
    var header_buf: [some_facts.fixed_size_header_size]u8 = undefined;

    @memcpy(header_buf[header_cursor..][0..@sizeOf(FileHeader)], std.mem.asBytes(&FileHeader{}));
    header_cursor += @sizeOf(FileHeader);

    @memcpy(header_buf[header_cursor..][0..@sizeOf(State)], std.mem.asBytes(&g.the_database.state));
    header_cursor += @sizeOf(State);

    var iovec_array: [1 + some_facts.number_of_dynamically_sized_sections]IovecConst = undefined;
    var i: usize = 0;
    iovec_array[i] = .{
        .iov_base = &header_buf,
        .iov_len = header_buf.len,
    };
    i += 1;
    {
        @memcpy(header_buf[header_cursor..][0..@sizeOf(u32)], std.mem.asBytes(&g.strings.len()));
        header_cursor += @sizeOf(u32);
        const slice = g.strings.buf.items;
        iovec_array[i] = .{
            .iov_base = slice.ptr,
            .iov_len = slice.len,
        };
        i += 1;
    }

    inline for (all_databases) |d| {
        if (!@TypeOf(d.*).should_save_to_disk) continue;

        @memcpy(header_buf[header_cursor..][0..@sizeOf(u32)], std.mem.asBytes(&@as(u32, @intCast(d.table.count()))));
        header_cursor += @sizeOf(u32);
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
    assert(header_cursor == header_buf.len);

    try file.writevAll(&iovec_array);

    log.info("write db bytes: {}", .{totalIovecLen(&iovec_array)});
}

//===== new API v3 for real this time =====

pub fn Database(
    comptime Key: type,
    comptime Value: type,
    comptime name: SubscriptionTag,
    comptime _should_save_to_disk: bool,
) type {
    return struct {
        const Self = @This();
        pub const should_save_to_disk = _should_save_to_disk;
        pub const subscription = name;

        version: Id = .{ .value = 0 },
        table: AutoArrayHashMapUnmanaged(Key, Value) = .{},

        last_version: Id = .{ .value = 0 },
        added_keys: AutoArrayHashMapUnmanaged(Key, void) = .{},
        removed_keys: AutoArrayHashMapUnmanaged(Key, void) = .{},
        modified_entries: AutoArrayHashMapUnmanaged(Key, Value) = .{},

        pub fn deinit(self: *@This()) void {
            self.table.deinit(g.gpa);
            self.* = undefined;
        }

        pub fn get(self: @This(), key: Key) *const Value {
            return self.table.getEntry(key).?.value_ptr;
        }
        pub fn getOrNull(self: @This(), key: Key) ?*const Value {
            return (self.table.getEntry(key) orelse return null).value_ptr;
        }
        pub fn getForEditing(self: *@This(), key: Key) !*Value {
            const entry = self.table.getEntry(key).?;
            try self.markModified(entry);
            return entry.value_ptr;
        }
        pub fn remove(self: *@This(), key: Key) !void {
            try self.removed_keys.putNoClobber(g.gpa, key, {});
            assert(self.table.swapRemove(key));
        }
        pub fn putNoClobber(self: *@This(), key: Key, value: Value) !void {
            try self.table.ensureUnusedCapacity(g.gpa, 1);
            try self.added_keys.putNoClobber(g.gpa, key, {});
            self.table.putAssumeCapacityNoClobber(key, value);
        }
        /// TODO: every use of this function is probably an antipattern that
        /// really needs a new method to avoid looking up a key multiple times.
        pub fn contains(self: @This(), key: Key) bool {
            return self.table.contains(key);
        }
        /// Generate a random key that does not collide with anything, put the value, and return the key.
        pub fn putRandom(self: *@This(), value: Value) !Key {
            try self.table.ensureUnusedCapacity(g.gpa, 1);
            try self.added_keys.ensureUnusedCapacity(g.gpa, 1);
            for (0..10) |_| {
                var key = Key.random(); // If you use putRandom(), this needs to be a function.
                const gop = self.table.getOrPutAssumeCapacity(key);
                if (gop.found_existing) {
                    // This is a @setCold path. See https://github.com/ziglang/zig/issues/5177 .
                    log.warn("Rerolling random id to avoid collisions", .{});
                    continue;
                }
                gop.value_ptr.* = value;
                self.added_keys.putAssumeCapacityNoClobber(key, {});
                return key;
            }
            return error.OverfullIdSpace; // tried to generate a random number, but it kept colliding with another one.
        }

        pub fn iterator(self: *@This()) Iterator {
            return .{
                .database = self,
                .sub_it = self.table.iterator(),
            };
        }

        pub const EntryConst = struct {
            key_ptr: *const Key,
            value_ptr: *const Value,
        };
        pub const Entry = AutoArrayHashMapUnmanaged(Key, Value).Entry;
        pub const Iterator = struct {
            database: *Self,
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
            pub fn promoteForEditing(self: @This(), kv: EntryConst) !*Value {
                assert(kv.key_ptr == self.current_kv.?.key_ptr);
                try self.database.markModified(self.current_kv.?);
                return self.current_kv.?.value_ptr;
            }
        };

        fn markModified(self: *@This(), entry: Entry) !void {
            const gop = try self.modified_entries.getOrPut(g.gpa, entry.key_ptr.*);
            if (gop.found_existing) return; // already noted.
            gop.value_ptr.* = entry.value_ptr.*;
        }

        pub fn reduceChanges(self: *@This()) bool {
            for (self.removed_keys.keys()) |key| {
                _ = self.added_keys.swapRemove(key);
                _ = self.modified_entries.swapRemove(key);
                assert(!self.table.contains(key));
            }
            for (self.added_keys.keys()) |key| {
                _ = self.modified_entries.swapRemove(key);
            }

            var i: usize = self.modified_entries.count();
            while (i > 0) : (i -= 1) {
                const key = self.modified_entries.keys()[i - 1];
                const original_value = self.modified_entries.values()[i - 1];
                const modern_value = self.table.get(key).?;
                if (deepEquals(original_value, modern_value)) {
                    // Actually, it's unmodified.
                    self.modified_entries.swapRemoveAt(i - 1);
                }
            }

            const is_dirty = self.added_keys.count() | self.removed_keys.count() | self.modified_entries.count() != 0;
            if (is_dirty) {
                self.last_version = self.version;
                self.version = Id.random();
                log.debug("new version for {s}: {}, last: {}", .{
                    @tagName(subscription), self.version, self.last_version,
                });
            }
            return is_dirty;
        }

        pub fn clearChanges(self: *@This()) void {
            self.removed_keys.clearRetainingCapacity();
            self.added_keys.clearRetainingCapacity();
            self.modified_entries.clearRetainingCapacity();
        }
    };
}

pub fn flushChanges() !void {
    var anything_to_broadcast = false;
    var anything_to_save_to_disk = false;
    inline for (all_databases) |d| {
        if (d.reduceChanges()) {
            anything_to_broadcast = true;
            if (@TypeOf(d.*).should_save_to_disk) {
                anything_to_save_to_disk = true;
            }
        }
    }
    var state_changed = false;
    if (!deepEquals(g.the_database.previous_state, g.the_database.state)) {
        anything_to_broadcast = true;
        anything_to_save_to_disk = true;
        state_changed = true;
        g.the_database.state_version = Id.random();
    }

    if (anything_to_broadcast) {
        try subscriptions.broadcastAllChanges();
    }
    if (anything_to_save_to_disk) {
        try save();
    }

    inline for (all_databases) |d| {
        d.clearChanges();
    }
    if (state_changed) {
        g.the_database.previous_state = g.the_database.state;
        g.the_database.previous_state_version = g.the_database.state_version;
    }
}

fn checkForCorruption(map: anytype) !void {
    for (map.values()) |record| {
        inline for (@typeInfo(@TypeOf(record)).Struct.fields) |field| {
            const int_value = if (field.type == StringPool.Index)
                @intFromEnum(@field(record, field.name))
            else if (field.type == StringPool.OptionalIndex)
                (if (@field(record, field.name) == .none) 0 else @intFromEnum(@field(record, field.name)))
            else
                continue;
            if (int_value >= g.strings.len()) return error.DataCorruption; // string pool index out of bounds
        }
    }
}

/// The purpose of this is to see if flashing this type to disk would cause any change.
fn deepEquals(a: anytype, b: @TypeOf(a)) bool {
    switch (@typeInfo(@TypeOf(a))) {
        .Void => return true,
        .Bool, .Int, .Float, .Enum => return a == b,

        .Optional => {
            return if (a != null and b != null)
                deepEquals(a.?, b.?)
            else if (a != null)
                false
            else if (b != null)
                false
            else
                true;
        },

        .Array => {
            for (a, b) |a_item, b_item| {
                if (!deepEquals(a_item, b_item)) return false;
            }
            return true;
        },

        .Struct => |struct_info| {
            inline for (struct_info.fields) |field| {
                if (!deepEquals(@field(a, field.name), @field(b, field.name))) return false;
            }
            return true;
        },

        .Union => |union_info| {
            const Tag = union_info.tag_type.?;
            if (@as(Tag, a) != @as(Tag, b)) return false;
            switch (a) {
                inline else => |a_value, tag| {
                    if (!deepEquals(a_value, @field(b, @tagName(tag)))) return false;
                },
            }
            return true;
        },

        else => @compileError("can't deepEquals for type: " ++ @typeName(@TypeOf(a))),
    }
}
