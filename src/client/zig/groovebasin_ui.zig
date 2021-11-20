const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;

const dom = @import("dom.zig");
const ws = @import("websocket_handler.zig");
const callback = @import("callback.zig");
const g = @import("global.zig");

const protocol = @import("shared").protocol;
const Track = protocol.Track;
const Library = @import("shared").Library;

pub const LoadStatus = enum {
    init,
    no_connection,
    good,
};

var main_grid_dom: i32 = undefined;
var main_err_msg_dom: i32 = undefined;
var main_err_msg_text_dom: i32 = undefined;

var library_artists_dom: i32 = undefined;
var empty_library_message_dom: i32 = undefined;
var library_no_items_dom: i32 = undefined;

var lag_display_dom: i32 = undefined;

const icon_collapsed = "icon-triangle-1-e";
const icon_expanded = "icon-triangle-1-se";

pub fn init() void {
    main_grid_dom = dom.getElementById("main-grid");
    main_err_msg_dom = dom.getElementById("main-err-msg");
    main_err_msg_text_dom = dom.getElementById("main-err-msg-text");

    library_artists_dom = dom.getElementById("library-artists");
    empty_library_message_dom = dom.getElementById("empty-library-message");
    library_no_items_dom = dom.getElementById("library-no-items");

    // this doesn't actually belong here.
    lag_display_dom = dom.getElementById("nowplaying-time-elapsed");

    library = Library{
        .strings = .{ .strings = ArrayList(u8).init(g.gpa) },
        .tracks = AutoArrayHashMap(u64, Track).init(g.gpa),
    };
}

pub fn setLoadingState(state: LoadStatus) void {
    const show_ui = state == .good;
    dom.setShown(main_grid_dom, show_ui);
    dom.setShown(main_err_msg_dom, !show_ui);
    if (state != .good) {
        // dom.setDocumentTitle(BASE_TITLE);
        dom.setTextContent(main_err_msg_text_dom, switch (state) {
            .init => "Loading...",
            .no_connection => "Server is down.",
            .good => unreachable,
        });
    }
}

pub fn setLag(lag_ns: i128) void {
    var buf: [100]u8 = undefined;
    dom.setTextContent(lag_display_dom, std.fmt.bufPrint(&buf, "lag: {d}ns", .{lag_ns}) catch {
        @panic("got an error");
    });
}

pub fn renderLibrary() void {
    dom.setTextContent(empty_library_message_dom, if (true) "No Results" else "loading...");
    dom.setShown(library_no_items_dom, library.tracks.count() == 0);

    // Delete and recreate all items.
    {
        var library_dom_element_count = dom.getChildrenCount(library_artists_dom);
        while (library_dom_element_count > 0) {
            dom.removeLastChild(library_artists_dom);
            library_dom_element_count -= 1;
        }
    }
    for (library.tracks.values()) |track, i| {
        // artist
        dom.insertAdjacentHTML(library_artists_dom, .beforeend,
            \\<li>
            \\  <div class="clickable expandable" data-type="artist">
            \\    <div class="icon"></div>
            \\    <span></span>
            \\  </div>
            \\  <ul></ul>
            \\</li>
        );
        const artist_li = dom.getChild(library_artists_dom, @intCast(i32, i));

        {
            const artist_div = dom.getChild(artist_li, 0);

            const icon_div = dom.getChild(artist_div, 0);
            dom.addClass(icon_div, icon_collapsed);
            dom.removeClass(icon_div, icon_expanded);

            const artist_span = dom.getChild(artist_div, 1);
            dom.setTextContent(artist_span, library.getString(track.artist));
        }

        const albums_ul = dom.getChild(artist_li, 1);

        // album
        dom.insertAdjacentHTML(albums_ul, .beforeend,
            \\<li>
            \\  <div class="clickable expandable" data-type="album">
            \\    <div class="icon"></div>
            \\    <span></span>
            \\  </div>
            \\  <ul></ul>
            \\</li>
        );
        const album_li = dom.getChild(albums_ul, 0);

        {
            const album_div = dom.getChild(album_li, 0);

            const icon_div = dom.getChild(album_div, 0);
            dom.addClass(icon_div, icon_collapsed);
            dom.removeClass(icon_div, icon_expanded);

            const album_span = dom.getChild(album_div, 1);
            dom.setTextContent(album_span, library.getString(track.album));
        }

        const tracks_ul = dom.getChild(album_li, 1);

        // track
        dom.insertAdjacentHTML(tracks_ul, .beforeend,
            \\<li>
            \\  <div class="clickable" data-type="track">
            \\    <span></span>
            \\  </div>
            \\</li>
        );

        const track_li = dom.getChild(tracks_ul, 0);
        const track_div = dom.getChild(track_li, 0);
        const track_span = dom.getChild(track_div, 0);
        dom.setTextContent(track_span, library.getString(track.title));
    }
}

var last_library_version: u64 = 0;
var library: Library = undefined;

pub fn poll() !void {
    var query_call = try ws.Call.init(.query);
    try query_call.writer().writeStruct(protocol.QueryRequest{
        .last_library = last_library_version,
    });
    try query_call.send(&handleQueryResponseCallback, undefined);
}
fn handleQueryResponseCallback(context: *callback.Context, response: []const u8) void {
    _ = context;
    handleQueryResponse(response) catch |err| {
        @panic(@errorName(err));
    };
}
fn handleQueryResponse(response: []const u8) !void {
    var stream = std.io.fixedBufferStream(response);
    const reader = stream.reader();
    const response_header = try reader.readStruct(protocol.QueryResponseHeader);
    if (response_header.library_version == last_library_version) return;

    const library_header = try reader.readStruct(protocol.LibraryHeader);
    // string pool
    try library.strings.strings.resize(library_header.string_size);
    try reader.readNoEof(library.strings.strings.items);
    // track keys and values
    library.tracks.clearRetainingCapacity();
    try library.tracks.ensureTotalCapacity(library_header.track_count);
    var key_stream = std.io.fixedBufferStream(response[stream.pos..]);
    const key_reader = key_stream.reader();
    var value_stream = std.io.fixedBufferStream(response[stream.pos + @sizeOf(u64) * library_header.track_count ..]);
    const value_reader = value_stream.reader();

    var i: u32 = 0;
    while (i < library_header.track_count) : (i += 1) {
        try library.tracks.putNoClobber(
            try key_reader.readIntLittle(u64),
            try value_reader.readStruct(Track),
        );
    }

    last_library_version = response_header.library_version;

    renderLibrary();
}
