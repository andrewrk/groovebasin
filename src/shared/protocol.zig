pub const RequestHeader = extern struct {
    /// An arbitrary number that will be included in the corresponding Response.
    /// The top bit must be unset, i.e. seq_id &= 0x7fff_ffff;
    seq_id: u32,
    op: Opcode,
};

pub const Opcode = enum(u8) {
    ping = 0,
    query = 1,
    enqueue = 2,
    send_chat = 3,
};

pub const ResponseHeader = extern struct {
    /// The seq_id of the corresponding request.
    /// If the top bit is set (i.e. (seq_id & 0x8000_0000) != 0),
    /// then this is actually a message from the server, not a response to a request.
    seq_id: u32,
    // followed by:
    //  more data depending on the opcode of the corresponding request if this is a response to a request.
    //  or nothing if this is a message from the server.
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

pub const EnqueueRequestHeader = extern struct {
    track_key: u64,
};

pub const SendChatRequestHeader = extern struct {
    msg_len: u32,
    // followed by:
    //  msg: [msg_len]u8,
};
