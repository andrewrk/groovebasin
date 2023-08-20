pub const Track = extern struct {
    file_path: u32,
    title: u32,
    artist: u32,
    album: u32,
    track_number: i16,
};

pub const Event = extern struct {
    sort_key: u64, // TODO: switch to a keese string.
    // these are all chat messages.
    name: u32,
    content: u32,
};
