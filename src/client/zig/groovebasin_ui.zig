const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;

const dom = @import("dom.zig");
const ws = @import("websocket_handler.zig");
const callback = @import("callback.zig");
const browser = @import("browser.zig");
const g = @import("global.zig");

const protocol = @import("shared").protocol;
const Track = protocol.Track;
const QueueItem = protocol.QueueItem;
const Library = @import("shared").Library;
const Queue = @import("shared").Queue;

pub const LoadStatus = enum {
    init,
    no_connection,
    good,
};

// layout
var main_grid_dom: i32 = undefined;
var main_err_msg_dom: i32 = undefined;
var main_err_msg_text_dom: i32 = undefined;

// left window
var library_artists_dom: i32 = undefined;
var empty_library_message_dom: i32 = undefined;
var library_no_items_dom: i32 = undefined;

// queue
var queue_items_div: i32 = undefined;

// now playing
var lag_display_dom: i32 = undefined;

const icon_collapsed = "icon-triangle-1-e";
const icon_expanded = "icon-triangle-1-se";

pub fn init() void {
    main_grid_dom = dom.getElementById("main-grid");
    main_err_msg_dom = dom.getElementById("main-err-msg");
    main_err_msg_text_dom = dom.getElementById("main-err-msg-text");

    library_artists_dom = dom.getElementById("library-artists");
    dom.addEventListener(library_artists_dom, .mousedown, &onLibraryMouseDownCallback, undefined);
    empty_library_message_dom = dom.getElementById("empty-library-message");
    library_no_items_dom = dom.getElementById("library-no-items");

    queue_items_div = dom.getElementById("queue-items");

    // this doesn't actually belong here.
    lag_display_dom = dom.getElementById("nowplaying-time-elapsed");

    library = Library{
        .strings = .{ .strings = ArrayList(u8).init(g.gpa) },
        .tracks = AutoArrayHashMap(u64, Track).init(g.gpa),
    };

    queue = Queue{
        .items = AutoArrayHashMap(u64, QueueItem).init(g.gpa),
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
        const track_key_str = formatKey(library.tracks.keys()[i]);

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
            dom.setAttribute(artist_div, "data-track", &track_key_str);

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
            dom.setAttribute(album_div, "data-track", &track_key_str);

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
        dom.setAttribute(track_div, "data-track", &track_key_str);
        const track_span = dom.getChild(track_div, 0);
        dom.setTextContent(track_span, library.getString(track.title));
    }
}

fn renderQueue() void {
    // Delete and recreate all items.
    {
        var c = dom.getChildrenCount(queue_items_div);
        while (c > 0) {
            dom.removeLastChild(queue_items_div);
            c -= 1;
        }
    }
    for (queue.items.values()) |item, i| {
        const track = library.tracks.get(item.track_key).?;
        dom.insertAdjacentHTML(queue_items_div, .beforeend,
            \\<div class="pl-item">
            \\  <span class="track"></span>
            \\  <span class="time"></span>
            \\  <span class="middle">
            \\    <span class="title"></span>
            \\    <span class="artist"></span>
            \\    <span class="album"></span>
            \\  </span>
            \\</div>
        );
        const item_div = dom.getChild(queue_items_div, @intCast(i32, i));

        // track
        dom.setTextContent(dom.getChild(item_div, 0), "42");
        // time
        dom.setTextContent(dom.getChild(item_div, 1), "3:69");

        const middle_div = dom.getChild(item_div, 2);
        // title
        dom.setTextContent(dom.getChild(middle_div, 0), library.getString(track.title));
        // artist
        dom.setTextContent(dom.getChild(middle_div, 1), library.getString(track.artist));
        // album
        dom.setTextContent(dom.getChild(middle_div, 2), library.getString(track.album));
    }
}

var last_library_version: u64 = 0;
var library: Library = undefined;

var last_queue_version: u64 = 0;
var queue: Queue = undefined;

pub fn poll() !void {
    var query_call = try ws.Call.init(.query);
    try query_call.writer().writeStruct(protocol.QueryRequest{
        .last_library = last_library_version,
        .last_queue = last_queue_version,
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
    const response_header = try stream.reader().readStruct(protocol.QueryResponseHeader);
    var render_library = false;
    var render_queue = false;

    // Library
    if (response_header.library_version != last_library_version) {
        const library_header = try stream.reader().readStruct(protocol.LibraryHeader);
        // string pool
        try library.strings.strings.resize(library_header.string_size);
        try stream.reader().readNoEof(library.strings.strings.items);
        // track keys and values
        library.tracks.clearRetainingCapacity();
        try library.tracks.ensureTotalCapacity(library_header.track_count);
        var key_stream = std.io.fixedBufferStream(response[stream.pos..]);
        var value_stream = std.io.fixedBufferStream(response[stream.pos + @sizeOf(u64) * library_header.track_count ..]);

        var i: u32 = 0;
        while (i < library_header.track_count) : (i += 1) {
            try library.tracks.putNoClobber(
                try key_stream.reader().readIntLittle(u64),
                try value_stream.reader().readStruct(Track),
            );
        }
        stream.pos += library_header.track_count * @sizeOf(u64) + library_header.track_count * @sizeOf(Track);

        last_library_version = response_header.library_version;
        render_library = true;
    }

    // Queue
    if (response_header.queue_version != last_queue_version) {
        const queue_header = try stream.reader().readStruct(protocol.QueueHeader);
        // item keys and values
        queue.items.clearRetainingCapacity();
        // try queue.items.ensureTotalCapacity(queue_header.item_count);
        var key_stream = std.io.fixedBufferStream(response[stream.pos..]);
        var value_stream = std.io.fixedBufferStream(response[stream.pos + @sizeOf(u64) * queue_header.item_count ..]);

        var i: u32 = 0;
        while (i < queue_header.item_count) : (i += 1) {
            try queue.items.putNoClobber(
                try key_stream.reader().readIntLittle(u64),
                try value_stream.reader().readStruct(QueueItem),
            );
        }
        stream.pos += queue_header.item_count * @sizeOf(u64) + queue_header.item_count * @sizeOf(QueueItem);

        last_queue_version = response_header.queue_version;
        render_queue = true;
    }

    if (render_library) renderLibrary();
    if (render_queue) renderQueue();
}

fn onLibraryMouseDownCallback(context: *callback.Context, event: i32) void {
    _ = context;
    onLibraryMouseDown(event) catch |err| {
        @panic(@errorName(err));
    };
}

fn onLibraryMouseDown(event: i32) !void {
    var target = dom.getEventTarget(event);
    target = dom.searchAncestorsForClass(target, library_artists_dom, "clickable");
    if (target == library_artists_dom) return;
    var track_key_str: [16]u8 = undefined;
    dom.readAttribute(target, "data-track", &track_key_str);
    const track_key = parseKey(track_key_str);
    browser.printHex("enqueuing: ", std.mem.asBytes(&track_key));

    var query_call = try ws.Call.init(.enqueue);
    try query_call.writer().writeStruct(protocol.EnqueueRequestHeader{
        .track_key = track_key,
    });
    try query_call.send(&ws.ignoreResponseCallback, undefined);
}

fn formatKey(key: u64) [16]u8 {
    var ret: [16]u8 = undefined;
    std.debug.assert(std.fmt.formatIntBuf(&ret, key, 16, .lower, .{ .width = 16, .fill = '0' }) == 16);
    return ret;
}
fn parseKey(str: [16]u8) u64 {
    return std.fmt.parseUnsigned(u64, &str, 16) catch |err| {
        @panic(@errorName(err));
    };
}

pub fn renderButtonIsOn(button: i32, is_on: bool) void {
    if (is_on) {
        dom.addClass(button, "on");
    } else {
        dom.removeClass(button, "on");
    }
}
