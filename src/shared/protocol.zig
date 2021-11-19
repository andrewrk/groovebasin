pub const RequestHeader = extern struct {
    // An arbitrary number that will be included in the corresponding Response.
    seq_id: u32,
    op: Opcode,
};

pub const Opcode = enum(u8) {
    ping = 0,
    query = 1,
};

pub const ResponseHeader = extern struct {
    // The seq_id of the corresponding Request.
    seq_id: u32,
};

pub const Track = extern struct {
    file_path: u32,
    title: u32,
    artist: u32,
    album: u32,
};

pub const QueryRequest = extern struct {
    last_library: u64,
};
