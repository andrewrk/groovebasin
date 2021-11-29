const std = @import("std");

const callback = @import("callback.zig");
const browser = @import("browser.zig");
const ui = @import("groovebasin_ui.zig");
const env = @import("browser_env.zig");
const dom = @import("dom.zig");
const g = @import("global.zig");

var stream_button: i32 = undefined;
var stream_button_label: i32 = undefined;
var client_volume_div: i32 = undefined;
var client_volume_slider: i32 = undefined;

var stream_audio_handle: i32 = undefined;

var stream_state: enum {
    off,
    paused,
    buffering,
    on,
} = .off;

pub fn init() void {
    stream_button = dom.getElementById("stream-btn");
    dom.addEventListener(stream_button, .click, &onStreamButtonClick, undefined);
    stream_button_label = dom.getElementById("stream-btn-label");
    client_volume_div = dom.getElementById("client-vol");
    client_volume_slider = dom.getElementById("client-vol-slider");
    dom.addEventListener(client_volume_slider, .change, &onClientVolumeSliderChange, undefined);
    dom.addEventListener(client_volume_slider, .input, &onClientVolumeSliderChange, undefined);

    stream_audio_handle = env.newAudio();
    dom.addEventListener(stream_audio_handle, .playing, &onStreamAudioPlaying, undefined);

    // TODO: load from localStorage.
    if (@as(?f64, 1.0)) |initial_client_volume| {
        dom.setInputValueAsNumber(client_volume_slider, initial_client_volume);
        env.setAudioVolume(stream_audio_handle, initial_client_volume);
    }
}

fn renderStreamButton() void {
    // TODO: show number of streamers.
    var buf: [30]u8 = undefined;
    // FIXME: inlining this switch into the fmt tuple always produces "Off".
    const state_str: []const u8 = switch (stream_state) {
        .off => "Off",
        .paused => "Paused",
        .buffering => "Buffering",
        .on => "On",
    };
    const label = std.fmt.bufPrint(&buf, "Stream: {s}", .{state_str}) catch unreachable;

    dom.setTextContent(stream_button_label, label);
    const is_on = stream_state != .off;
    ui.renderButtonIsOn(stream_button, is_on);
    dom.setShown(client_volume_div, is_on);
}

fn handleClientVolumeSliderChange() void {
    const volume = dom.getInputValueAsNumber(client_volume_slider);
    env.setAudioVolume(stream_audio_handle, volume);
    // TODO: save to localStorage.
}

fn onStreamButtonClick(context: *callback.Context, event: i32) void {
    _ = context;
    _ = event;

    toggleStreamButton();
}
fn toggleStreamButton() void {
    if (stream_state == .off) {
        // Start streaming.
        browser.setAudioSrc(stream_audio_handle, "stream.mp3");
        env.loadAudio(stream_audio_handle);
        env.playAudio(stream_audio_handle);

        stream_state = .buffering;
    } else {
        env.pauseAudio(stream_audio_handle);
        browser.setAudioSrc(stream_audio_handle, "");
        env.loadAudio(stream_audio_handle);

        stream_state = .off;
    }
    renderStreamButton();
}

fn onClientVolumeSliderChange(context: *callback.Context, event: i32) void {
    _ = context;
    _ = event;

    handleClientVolumeSliderChange();
}

fn onStreamAudioPlaying(context: *callback.Context, event: i32) void {
    _ = context;
    _ = event;

    std.debug.assert(stream_state == .buffering);
    stream_state = .on;
    renderStreamButton();
}
