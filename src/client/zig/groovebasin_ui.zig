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
const model = @import("model.zig");
const g = @import("global.zig");

const protocol = @import("shared").protocol;
const Track = protocol.Track;
const QueueItem = protocol.QueueItem;
const Event = protocol.Event;
const StringPool = @import("shared").StringPool;

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

    model.init();
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
    dom.setShown(left_window_tabs[left_window_active_tab].pane, false);

    left_window_active_tab = index;
    dom.addClass(left_window_tabs[left_window_active_tab].tab, "active");
    dom.setShown(left_window_tabs[left_window_active_tab].pane, true);
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
    dom.setShown(library_no_items_dom, model.library.tracks.count() == 0);

    // Delete and recreate all items.
    {
        var library_dom_element_count = dom.getChildrenCount(library_artists_dom);
        while (library_dom_element_count > 0) {
            dom.removeLastChild(library_artists_dom);
            library_dom_element_count -= 1;
        }
    }
    for (model.library.tracks.values()) |track, i| {
        const track_key_str = formatKey(model.library.tracks.keys()[i]);

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
            dom.setTextContent(artist_span, model.library.getString(track.artist));
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
            dom.setTextContent(album_span, model.library.getString(track.album));
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
        dom.setTextContent(track_span, model.library.getString(track.title));
    }
}

pub fn renderQueue() void {
    // Delete and recreate all items.
    {
        var c = dom.getChildrenCount(queue_items_div);
        while (c > 0) {
            dom.removeLastChild(queue_items_div);
            c -= 1;
        }
    }
    for (model.queue.items.values()) |item, i| {
        const track = model.library.tracks.get(item.track_key).?;
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
        dom.setTextContent(dom.getChild(middle_div, 0), model.library.getString(track.title));
        // artist
        dom.setTextContent(dom.getChild(middle_div, 1), model.library.getString(track.artist));
        // album
        dom.setTextContent(dom.getChild(middle_div, 2), model.library.getString(track.album));
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
    const events_list = model.events.events.values();
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
        dom.setTextContent(name_span, model.events.getString(event.name));
        //dom.setTitle(name_span, some date represnetation);

        const msg_span = dom.getChild(event_div, 1);
        dom.setTextContent(msg_span, model.events.getString(event.content));
    }
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
            const msg = std.mem.trim(u8, text, &std.ascii.spaces);
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
