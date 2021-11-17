const json = @import("std").json;

pub const Request = struct {
    // An arbitrary number that will be included in the corresponding Response.
    seq: u32,
    op: Opcode,
};

pub const Opcode = enum {
    ping,
    _unused1, // avoid trivial enums to better test serialization.
    _unused2,

    pub fn jsonStringify(self: @This(), options: json.StringifyOptions, out_stream: anytype) @TypeOf(out_stream).Error!void {
        try json.stringify(@tagName(self), options, out_stream);
    }
};

pub const Response = struct {
    // The seq of the corresponding Request.
    seq: u32,

    data: ResponseData,
};

pub const ResponseData = union(enum) {
    ping: Timestamp,
    _unused1: u32,
    _unused2: bool,
};

pub const Timestamp = struct {
    /// Seconds of UTC time since Unix epoch 1970-01-01T00:00:00Z.
    s: i64,
    /// Nanoseconds in the range 0...999_999_999 inclusive.
    ns: i32,
};
