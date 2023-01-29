const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const AutoArrayHashMap = std.AutoArrayHashMap;

const dom = @import("dom.zig");
const enums = @import("browser_enums.zig");
const getModifier = enums.getModifier;
const KeyboardEventCode = enums.KeyboardEventCode;
const EventModifierKey = enums.EventModifierKey;

const ws = @import("websocket_handler.zig");
const callback = @import("callback.zig");
const browser = @import("browser.zig");
const g = @import("global.zig");

const protocol = @import("shared").protocol;
const Track = protocol.Track;
const QueueItem = protocol.QueueItem;
const Library = @import("shared").Library;
const Queue = @import("shared").Queue;
const Events = @import("shared").Events;
const Event = protocol.Event;

const log = std.log.scoped(.ui);

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
var left_window_tabs: [5]TabUi = undefined;
var left_window_active_tab: usize = 0;
var library_filter_textbox: i32 = undefined;
var library_artists_dom: i32 = undefined;
var empty_library_message_dom: i32 = undefined;
var library_no_items_dom: i32 = undefined;
var events_list_div: i32 = undefined;
var chat_textbox: i32 = undefined;

// queue
var queue_items_div: i32 = undefined;

// now playing
var lag_display_dom: i32 = undefined;

// popup
var blackout_div: i32 = undefined;
var modal_div: i32 = undefined;
var modal_title_span: i32 = undefined;
var shortcuts_popup_content_div: i32 = undefined;

const icon_collapsed = "icon-triangle-1-e";
const icon_expanded = "icon-triangle-1-se";

pub fn init() void {
    dom.addWindowEventListener(.keydown, callback.packCallback(onWindowKeydown, {}));

    main_grid_dom = dom.getElementById("main-grid");
    main_err_msg_dom = dom.getElementById("main-err-msg");
    main_err_msg_text_dom = dom.getElementById("main-err-msg-text");

    left_window_tabs = [_]TabUi{
        .{
            .tab = dom.getElementById("library-tab"),
            .pane = dom.getElementById("library-pane"),
        },
        .{
            .tab = dom.getElementById("playlists-tab"),
            .pane = dom.getElementById("playlists-pane"),
        },
        .{
            .tab = dom.getElementById("upload-tab"),
            .pane = dom.getElementById("upload-pane"),
        },
        .{
            .tab = dom.getElementById("events-tab"),
            .pane = dom.getElementById("events-pane"),
        },
        .{
            .tab = dom.getElementById("settings-tab"),
            .pane = dom.getElementById("settings-pane"),
        },
    };
    for (left_window_tabs) |*tab_ui, i| {
        dom.addEventListener(tab_ui.tab, .click, callback.packCallback(onLeftWindowTabClick, i));
    }

    library_filter_textbox = dom.getElementById("lib-filter");
    dom.addEventListener(library_filter_textbox, .keydown, callback.packCallback(onLibraryFilterKeydown, {}));
    library_artists_dom = dom.getElementById("library-artists");
    dom.addEventListener(library_artists_dom, .mousedown, callback.packCallback(onLibraryMouseDown, {}));
    empty_library_message_dom = dom.getElementById("empty-library-message");
    library_no_items_dom = dom.getElementById("library-no-items");
    events_list_div = dom.getElementById("events-list");
    chat_textbox = dom.getElementById("chat-box-input");
    dom.addEventListener(chat_textbox, .keydown, callback.packCallback(onChatTextboxKeydown, {}));

    queue_items_div = dom.getElementById("queue-items");

    // this doesn't actually belong here.
    lag_display_dom = dom.getElementById("nowplaying-time-elapsed");

    blackout_div = dom.getElementById("blackout");
    dom.addEventListener(blackout_div, .keydown, callback.packCallback(onEscapeClosePopup, {}));
    dom.addEventListener(blackout_div, .click, callback.packCallback(onAnythingClosePopup, {}));
    modal_div = dom.getElementById("modal");
    modal_title_span = dom.getElementById("modal-title");
    shortcuts_popup_content_div = dom.getElementById("shortcuts");
    dom.addEventListener(modal_div, .keydown, callback.packCallback(onEscapeClosePopup, {}));
    dom.addEventListener(dom.getElementById("modal-close"), .click, callback.packCallback(onAnythingClosePopup, {}));

    library = Library{
        .strings = .{ .strings = ArrayList(u8).init(g.gpa) },
        .tracks = AutoArrayHashMap(u64, Track).init(g.gpa),
    };

    queue = Queue{
        .items = AutoArrayHashMap(u64, QueueItem).init(g.gpa),
    };

    events = Events{
        .strings = .{ .strings = ArrayList(u8).init(g.gpa) },
        .events = AutoArrayHashMap(u64, Event).init(g.gpa),
    };
}

const TabUi = struct {
    /// click this.
    tab: i32,
    /// shows this.
    pane: i32,
};

fn onLeftWindowTabClick(clicked_index: usize, event: i32) anyerror!void {
    const modifiers = dom.getEventModifiers(event);
    if (getModifier(modifiers, .alt)) return;
    dom.preventDefault(event);

    focusLeftWindowTab(clicked_index);
}

fn focusLeftWindowTab(index: usize) void {
    if (index == left_window_active_tab) return;
    dom.removeClass(left_window_tabs[left_window_active_tab].tab, "active");
    setShown(left_window_tabs[left_window_active_tab].pane, false);

    left_window_active_tab = index;
    dom.addClass(left_window_tabs[left_window_active_tab].tab, "active");
    setShown(left_window_tabs[left_window_active_tab].pane, true);
}

pub fn setLoadingState(state: LoadStatus) void {
    const show_ui = state == .good;
    setShown(main_grid_dom, show_ui);
    setShown(main_err_msg_dom, !show_ui);
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

pub fn setShown(element: i32, shown: bool) void {
    if (shown) {
        dom.removeClass(element, "hidden");
    } else {
        dom.addClass(element, "hidden");
    }
}

fn renderLibrary() !void {
    dom.setTextContent(empty_library_message_dom, if (true) "No Results" else "loading...");
    setShown(library_no_items_dom, library.tracks.count() == 0);

    var buf = std.ArrayList(u8).init(g.gpa);
    defer buf.deinit();

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
        const title = library.getString(track.title);
        dom.setTextContent(track_span, if (track.track_number != 0)
            try formatSingleUse(&buf, "{d}. {s}", .{ track.track_number, title })
        else
            title);
    }
}

fn formatSingleUse(buffer: *std.ArrayList(u8), comptime fmt: []const u8, args: anytype) ![]const u8 {
    buffer.clearRetainingCapacity();
    try std.fmt.format(buffer.writer(), fmt, args);
    return buffer.items;
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
    var short_buf: [256]u8 = undefined;
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
        dom.setTextContent(dom.getChild(item_div, 0), if (track.track_number != 0)
            std.fmt.bufPrint(short_buf[0..], "{d}", .{track.track_number}) catch unreachable
        else
            "");
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

pub fn renderEvents() void {
    // Delete and recreate all items.
    {
        var c = dom.getChildrenCount(events_list_div);
        while (c > 0) {
            dom.removeLastChild(events_list_div);
            c -= 1;
        }
    }
    const events_list = events.events.values();
    // TODO: sort by sort_key.
    for (events_list) |event, i| {
        dom.insertAdjacentHTML(events_list_div, .beforeend,
            \\<div class="event">
            \\  <span class="name"></span>
            \\  <span class="msg"></span>
            \\  <div style="clear: both;"></div>
            \\</div>
        );

        const event_div = dom.getChild(events_list_div, @intCast(i32, i));
        dom.addClass(event_div, "chat");

        const name_span = dom.getChild(event_div, 0);
        dom.setTextContent(name_span, events.getString(event.name));
        //dom.setTitle(name_span, some date represnetation);

        const msg_span = dom.getChild(event_div, 1);
        dom.setTextContent(msg_span, events.getString(event.content));
    }
}

var last_library_version: u64 = 0;
var library: Library = undefined;

var last_queue_version: u64 = 0;
var queue: Queue = undefined;

var last_events_version: u64 = 0;
var events: Events = undefined;

pub fn poll() !void {
    var query_call = try ws.Call.init(.query);
    try query_call.writer().writeStruct(protocol.QueryRequest{
        .last_library = last_library_version,
        .last_queue = last_queue_version,
        .last_events = last_events_version,
    });
    try query_call.send(handleQueryResponse, {});
}
fn handleQueryResponse(response: []const u8) anyerror!void {
    var stream = std.io.fixedBufferStream(response);
    const response_header = try stream.reader().readStruct(protocol.QueryResponseHeader);
    var render_library = false;
    var render_queue = false;
    var render_events = false;

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

    // Events
    if (response_header.events_version != last_events_version) {
        const events_header = try stream.reader().readStruct(protocol.EventsHeader);
        // string pool
        try events.strings.strings.resize(events_header.string_size);
        try stream.reader().readNoEof(events.strings.strings.items);
        // keys and values
        events.events.clearRetainingCapacity();
        try events.events.ensureTotalCapacity(events_header.item_count);
        var key_stream = std.io.fixedBufferStream(response[stream.pos..]);
        var value_stream = std.io.fixedBufferStream(response[stream.pos + @sizeOf(u64) * events_header.item_count ..]);

        var i: u32 = 0;
        while (i < events_header.item_count) : (i += 1) {
            try events.events.putNoClobber(
                try key_stream.reader().readIntLittle(u64),
                try value_stream.reader().readStruct(Event),
            );
        }
        stream.pos += events_header.item_count * @sizeOf(u64) + events_header.item_count * @sizeOf(Event);

        last_events_version = response_header.events_version;
        render_events = true;
    }

    if (render_library) try renderLibrary();
    if (render_queue) renderQueue();
    if (render_events) renderEvents();
}

fn onLibraryMouseDown(event: i32) anyerror!void {
    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    var arena = arena_instance.allocator();

    const modifiers = dom.getEventModifiers(event);
    if (getModifier(modifiers, .alt)) return;
    dom.preventDefault(event);

    var target = dom.getEventTarget(event);
    target = dom.searchAncestorsForClass(target, library_artists_dom, "clickable");
    if (target == library_artists_dom) return;
    const track_key = parseKey(dom.getAttribute(target, &arena, "data-track"));
    log.info("enqueuing: {}", .{std.fmt.fmtSliceHexLower(std.mem.asBytes(&track_key))});

    var query_call = try ws.Call.init(.enqueue);
    try query_call.writer().writeStruct(protocol.EnqueueRequestHeader{
        .track_key = track_key,
    });
    try query_call.send(ws.ignoreResponse, {});
}

fn formatKey(key: u64) [16]u8 {
    var ret: [16]u8 = undefined;
    std.debug.assert(std.fmt.formatIntBuf(&ret, key, 16, .lower, .{ .width = 16, .fill = '0' }) == 16);
    return ret;
}
fn parseKey(str: []const u8) u64 {
    return std.fmt.parseUnsigned(u64, str, 16) catch |err| {
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

fn getModifiersAndCode(event: i32) u64 {
    const modifiers = dom.getEventModifiers(event);
    const code = dom.getKeyboardEventCode(event);
    return @as(u64, @intCast(u31, modifiers)) << 32 | @intCast(u31, @enumToInt(code));
}
fn k(code: KeyboardEventCode) u64 {
    return @intCast(u31, @enumToInt(code));
}
fn k2(modifier: EventModifierKey, code: KeyboardEventCode) u64 {
    return (@as(u64, 1) << (@intCast(u6, @enumToInt(modifier)) + 32)) | @intCast(u31, @enumToInt(code));
}
fn k3(modifier1: EventModifierKey, modifier2: EventModifierKey, code: KeyboardEventCode) u64 {
    return (@as(u64, 1) << (@intCast(u6, @enumToInt(modifier1)) + 32)) | (@as(u64, 1) << (@intCast(u6, @enumToInt(modifier2)) + 32)) | @intCast(u31, @enumToInt(code));
}

fn onWindowKeydown(event: i32) anyerror!void {
    switch (getModifiersAndCode(event)) {
        k(.KeyT) => {
            focusLeftWindowTab(3); // Chat
            dom.focus(chat_textbox);
        },
        k(.Slash) => {
            focusLeftWindowTab(0); // Library
            dom.focus(library_filter_textbox);
        },
        k(.KeyS) => {
            @import("stream.zig").toggleStreamButton();
        },
        k2(.shift, .Slash) => {
            showPopup();
        },
        k2(.ctrl, .ArrowRight) => {
            log.info("TODO: next song", .{});
        },
        k3(.alt, .shift, .Enter) => {
            log.info("TODO: insert next shuffled.", .{});
        },
        else => return,
    }
    dom.preventDefault(event);
}

fn onLibraryFilterKeydown(event: i32) anyerror!void {
    dom.stopPropagation(event);

    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    var arena = arena_instance.allocator();

    switch (getModifiersAndCode(event)) {
        k(.Escape) => {
            const text = dom.getInputValue(library_filter_textbox, &arena);
            if (text.len == 0) {
                dom.blur(library_filter_textbox);
            } else {
                dom.setInputValue(library_filter_textbox, "");
            }
        },
        else => return,
    }
    dom.preventDefault(event);
}

fn onChatTextboxKeydown(event: i32) anyerror!void {
    dom.stopPropagation(event);

    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    var arena = arena_instance.allocator();

    switch (getModifiersAndCode(event)) {
        k(.Escape) => {
            dom.blur(chat_textbox);
        },
        k(.Enter), k(.NumpadEnter) => {
            // TODO: send a message
            const text = dom.getInputValue(chat_textbox, &arena);
            if (text.len > 0) {
                dom.setInputValue(chat_textbox, "");
            }
            const msg = std.mem.trim(u8, text, &std.ascii.whitespace);
            if (msg.len > 0) {
                if (msg[0] == '/') {
                    // TODO: support any commands
                } else {
                    var chat_call = try ws.Call.init(.send_chat);
                    try chat_call.writer().writeStruct(protocol.SendChatRequestHeader{
                        .msg_len = msg.len,
                    });
                    try chat_call.writer().writeAll(msg);
                    try chat_call.send(ws.ignoreResponse, {});
                }
            }
        },
        else => return,
    }
    dom.preventDefault(event);
}

fn showPopup() void {
    dom.setTextContent(modal_title_span, "Keyboard Shortcuts");
    setShown(shortcuts_popup_content_div, true);

    setShown(blackout_div, true);
    setShown(modal_div, true);

    dom.focus(shortcuts_popup_content_div);
}

fn closePopup() void {
    setShown(blackout_div, false);
    setShown(modal_div, false);
}

fn onEscapeClosePopup(event: i32) anyerror!void {
    dom.stopPropagation(event);
    switch (getModifiersAndCode(event)) {
        k(.Escape) => {
            closePopup();
        },
        else => return,
    }
    dom.preventDefault(event);
}

fn onAnythingClosePopup(_: i32) anyerror!void {
    closePopup();
}
