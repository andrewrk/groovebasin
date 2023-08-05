playlist: *Groove.Playlist,
encoder: *Groove.Encoder,

const std = @import("std");
const Player = @This();
const Groove = @import("groove.zig").Groove;
const SoundIo = @import("soundio.zig").SoundIo;
const g = @import("global.zig");
const fatal = @import("web_server.zig").fatal;
const log = std.log;

pub fn init(bit_rate_k: u32) !Player {
    const playlist = try g.groove.playlist_create();
    const encoder = try g.groove.encoder_create();
    const player = try g.groove.player_create();

    playlist.pause();

    const device_index = g.soundio.default_output_device_index();
    if (device_index < 0) fatal("output device not found", .{});
    const device = try g.soundio.get_output_device(device_index);
    log.info("output device: {s}", .{device.name});
    if (device.probe_error != 0) {
        fatal("unable to probe device: {s}", .{SoundIo.strerror(device.probe_error)});
    }
    player.device = device;
    player.name = "GrooveBasin";
    try player.attach(playlist);

    encoder.bit_rate = @as(c_int, @intCast(bit_rate_k * 1000));
    encoder.format_short_name = "mp3";
    encoder.codec_short_name = "mp3";
    try encoder.attach(playlist);

    playlist.play();

    return Player{
        .playlist = playlist,
        .encoder = encoder,
    };
}

pub fn deinit(player: *Player) void {
    player.encoder.destroy();
    player.playlist.destroy();
}
