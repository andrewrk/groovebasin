playlist: *Groove.Playlist,
encoder: *Groove.Encoder,

const Player = @This();
const Groove = @import("groove.zig").Groove;
const global = @import("global.zig");

pub fn init(bit_rate_k: u32) !Player {
    const playlist = try global.groove.playlist_create();
    const encoder = try global.groove.encoder_create();

    encoder.bit_rate = @intCast(c_int, bit_rate_k * 1000);
    encoder.format_short_name = "mp3";
    encoder.codec_short_name = "mp3";

    try encoder.attach(playlist);

    return Player{
        .playlist = playlist,
        .encoder = encoder,
    };
}

pub fn deinit(player: *Player) void {
    player.encoder.destroy();
    player.playlist.destroy();
}
