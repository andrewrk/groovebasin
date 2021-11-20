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

pub const QueryRequest = extern struct {
    last_library: u64,
    last_queue: u64,
};

pub const QueryResponseHeader = extern struct {
    library_version: u64,
    queue_version: u64,
    // followed by:
    //  LibraryHeader if library_version != last_library,
    //  QueueHeader if queue_version != last_queue,
};

pub const LibraryHeader = extern struct {
    string_size: u32,
    track_count: u32,
    // followed by:
    //  strings: [string_size]u8,
    //  track_keys: [track_count]u64,
    //  tracks: [track_count]Track,
};

pub const Track = extern struct {
    file_path: u32,
    title: u32,
    artist: u32,
    album: u32,
};

pub const QueueHeader = extern struct {
    item_count: u32,
    // followed by:
    //  item_keys: [item_count]u64,
    //  items: [item_count]QueueItem,
};

pub const QueueItem = extern struct {
    sort_key: u64, // TODO: switch to a keese string.
    track_key: u64,
};
