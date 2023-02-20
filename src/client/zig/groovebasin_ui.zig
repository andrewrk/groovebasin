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
const StringPool = @import("shared").StringPool;
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
    for (&left_window_tabs, 0..) |*tab_ui, i| {
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

    library = Library.init(g.gpa);

    queue = Queue{
        .items = AutoArrayHashMap(u64, QueueItem).init(g.gpa),
    };

    events = Events{
        .strings = StringPool.init(g.gpa),
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

    var html_buf = ArrayList(u8).init(g.gpa);
    defer html_buf.deinit();
    var current_artist: ?u32 = null;
    var current_album: ?u32 = null;

    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    var arena = arena_instance.allocator();

    const begin_artist = comptime minifyHtml(
        \\<li>
        \\  <div class="clickable expandable" data-type="artist">
        \\    <div class="icon icon-triangle-1-e"></div>
        \\    <span>{s}</span>
        \\  </div>
        \\  <ul>
    );
    const begin_album = comptime minifyHtml(
        \\    <li>
        \\      <div class="clickable expandable" data-type="album">
        \\        <div class="icon icon-triangle-1-e"></div>
        \\        <span>{s}</span>
        \\      </div>
        \\      <ul>
    );
    const track_html = comptime minifyHtml(
        \\        <li>
        \\          <div class="clickable" data-type="track" data-track="{s}">
        \\            <span>{s}</span>
        \\          </div>
        \\        </li>
    );
    const end_album = comptime minifyHtml(
        \\      </ul>
        \\    </li>
    );
    const end_artist = comptime minifyHtml(
        \\  </ul>
        \\</li>
    );

    dom.setInnerHtml(library_artists_dom, "");
    for (library.tracks.values(), 0..) |track, i| {
        _ = arena_instance.reset(.retain_capacity);

        const track_key_buf = formatKey(library.tracks.keys()[i]);
        const track_key_str = track_key_buf[0..];

        if (current_artist) |a| {
            if (a != track.artist) {
                // end album
                try html_buf.appendSlice(end_album);
                current_album = null;
                // end artist
                try html_buf.appendSlice(end_artist);
                current_artist = null;
                dom.insertAdjacentHTML(library_artists_dom, .beforeend, html_buf.items);
                html_buf.clearRetainingCapacity();
            }
        }
        if (current_album) |a| {
            if (a != track.album) {
                // end album
                try html_buf.appendSlice(end_album);
                current_album = null;
            }
        }
        if (current_artist == null) {
            // begin artist
            try std.fmt.format(html_buf.writer(), begin_artist, .{
                try escapeHtml(arena, library.getString(track.artist)),
            });
            current_artist = track.artist;
        }
        if (current_album == null) {
            // begin album
            try std.fmt.format(html_buf.writer(), begin_album, .{
                try escapeHtml(arena, library.getString(track.album)),
            });
            current_album = track.album;
        }
        try std.fmt.format(html_buf.writer(), track_html, .{
            track_key_str,
            try escapeHtml(arena, if (track.track_number != 0)
                try std.fmt.allocPrint(arena, "{d}. {s}", .{ track.track_number, library.getString(track.title) })
            else
                library.getString(track.title)),
        });
    }
    if (current_album != null) {
        try html_buf.appendSlice(end_album);
    }
    if (current_artist != null) {
        try html_buf.appendSlice(end_artist);
        dom.insertAdjacentHTML(library_artists_dom, .beforeend, html_buf.items);
    }
}

fn escapeHtml(allocator: std.mem.Allocator, s: []const u8) ![]const u8 {
    const problems = "\"'&<>";
    var problem_index = std.mem.indexOfAny(u8, s, problems) orelse return s;

    var buffer = std.ArrayList(u8).init(allocator);
    defer buffer.deinit();
    var i: usize = 0;
    while (true) {
        try buffer.appendSlice(s[i..problem_index]);
        switch (s[problem_index]) {
            '"' => try buffer.appendSlice("&quot;"),
            '\'' => try buffer.appendSlice("&apos;"),
            '&' => try buffer.appendSlice("&amp;"),
            '<' => try buffer.appendSlice("&lt;"),
            '>' => try buffer.appendSlice("&gt;"),
            else => unreachable,
        }
        i = problem_index + 1;
        problem_index = std.mem.indexOfAnyPos(u8, s, i, problems) orelse break;
    }
    try buffer.appendSlice(s[i..]);
    return buffer.toOwnedSlice();
}

fn minifyHtml(comptime s_: []const u8) []const u8 {
    // return s.trim().replaceAll(/>\s+/g, ">");
    comptime var s = std.mem.trim(u8, s_, " \n");
    comptime var index = 0;
    while (true) {
        index = 1 + (std.mem.indexOfScalarPos(u8, s, index, '>') orelse return s);
        s = s[0..index] ++ std.mem.trimLeft(u8, s[index..], " \n");
    }
}

fn renderQueue() !void {
    // Delete and recreate all items.
    dom.setInnerHtml(queue_items_div, "");

    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    var arena = arena_instance.allocator();

    var short_buf: [256]u8 = undefined;
    for (queue.items.values()) |item| {
        _ = arena_instance.reset(.retain_capacity);

        const track = library.tracks.get(item.track_key).?;
        dom.insertAdjacentHTML(queue_items_div, .beforeend, try std.fmt.allocPrint(arena,
            \\<div class="pl-item">
            \\  <span class="track">{s}</span>
            \\  <span class="time">{s}</span>
            \\  <span class="middle">
            \\    <span class="title">{s}</span>
            \\    <span class="artist">{s}</span>
            \\    <span class="album">{s}</span>
            \\  </span>
            \\</div>
        , .{
            // track
            if (track.track_number != 0)
                std.fmt.bufPrint(short_buf[0..], "{d}", .{track.track_number}) catch unreachable
            else
                "",
            // time
            "3:69",
            // title
            try escapeHtml(arena, library.getString(track.title)),
            // artist
            try escapeHtml(arena, library.getString(track.artist)),
            // album
            try escapeHtml(arena, library.getString(track.album)),
        }));
    }
}

pub fn renderEvents() !void {
    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    var arena = arena_instance.allocator();

    // Delete and recreate all items.
    dom.setInnerHtml(events_list_div, "");

    const events_list = events.events.values();
    // TODO: sort by sort_key.
    for (events_list) |event| {
        _ = arena_instance.reset(.retain_capacity);

        dom.insertAdjacentHTML(events_list_div, .beforeend, try std.fmt.allocPrint(arena,
            \\<div class="event chat">
            \\  <span class="name">{s}</span>
            \\  <span class="msg">{s}</span>
            \\  <div style="clear: both;"></div>
            \\</div>
        , .{
            try escapeHtml(arena, events.getString(event.name)),
            try escapeHtml(arena, events.getString(event.content)),
        }));
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
        try library.strings.buf.resize(library_header.string_size);
        try stream.reader().readNoEof(library.strings.buf.items);
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
        try events.strings.buf.resize(events_header.string_size);
        try stream.reader().readNoEof(events.strings.buf.items);
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
    if (render_queue) try renderQueue();
    if (render_events) try renderEvents();
}

fn onLibraryMouseDown(event: i32) anyerror!void {
    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    var arena = arena_instance.allocator();

    const modifiers = dom.getEventModifiers(event);
    if (modifiers != 0) return;
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
