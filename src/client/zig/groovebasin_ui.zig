const std = @import("std");

const dom = @import("dom.zig");

const protocol = @import("shared").protocol;

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

pub fn setLibrary(library: protocol.Library) void {
    dom.setTextContent(empty_library_message_dom, if (true) "No Results" else "loading...");
    dom.setShown(library_no_items_dom, library.tracks.len == 0);

    // Delete and recreate all items.
    {
        var library_dom_element_count = dom.getChildrenCount(library_artists_dom);
        while (library_dom_element_count > 0) {
            dom.removeLastChild(library_artists_dom);
            library_dom_element_count -= 1;
        }
    }
    for (library.tracks) |track, i| {
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
        defer dom.releaseElementHandle(artist_li);

        {
            const artist_div = dom.getChild(artist_li, 0);
            defer dom.releaseElementHandle(artist_div);

            const icon_div = dom.getChild(artist_div, 0);
            defer dom.releaseElementHandle(icon_div);
            dom.addClass(icon_div, icon_collapsed);
            dom.removeClass(icon_div, icon_expanded);

            const artist_span = dom.getChild(artist_div, 1);
            defer dom.releaseElementHandle(artist_span);
            dom.setTextContent(artist_span, track.artist);
        }

        const albums_ul = dom.getChild(artist_li, 1);
        defer dom.releaseElementHandle(albums_ul);

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
        defer dom.releaseElementHandle(album_li);

        {
            const album_div = dom.getChild(album_li, 0);
            defer dom.releaseElementHandle(album_div);

            const icon_div = dom.getChild(album_div, 0);
            defer dom.releaseElementHandle(icon_div);
            dom.addClass(icon_div, icon_collapsed);
            dom.removeClass(icon_div, icon_expanded);

            const album_span = dom.getChild(album_div, 1);
            defer dom.releaseElementHandle(album_span);
            dom.setTextContent(album_span, track.album);
        }

        const tracks_ul = dom.getChild(album_li, 1);
        defer dom.releaseElementHandle(tracks_ul);

        // track
        dom.insertAdjacentHTML(tracks_ul, .beforeend,
            \\<li>
            \\  <div class="clickable" data-type="track">
            \\    <span></span>
            \\  </div>
            \\</li>
        );

        const track_li = dom.getChild(tracks_ul, 0);
        defer dom.releaseElementHandle(track_li);

        const track_div = dom.getChild(track_li, 0);
        defer dom.releaseElementHandle(track_div);

        const track_span = dom.getChild(track_div, 0);
        defer dom.releaseElementHandle(track_span);

        dom.setTextContent(track_span, track.title);
    }
}
