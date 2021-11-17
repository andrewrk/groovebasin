comptime {
    _ = @import("callback.zig");
}
const websocket_handler = @import("websocket_handler.zig");
const browser = @import("browser.zig");
const dom = @import("dom.zig");

export fn main() void {
    loadDomElements();
    browser.print("zig: hello world");
    if (false) {
        browser.serveWebSocket(
            &websocket_handler.onOpen,
            undefined,
            &websocket_handler.onClose,
            undefined,
            &websocket_handler.onError,
            undefined,
            &websocket_handler.onMessage,
            undefined,
        );
    }

    setLoadingState(.good);
}

const LoadStatus = enum {
    init,
    no_connection,
    good,
};

var queue_window_dom: i32 = undefined;
var left_window_dom: i32 = undefined;
var now_playing_dom: i32 = undefined;
var main_err_msg_dom: i32 = undefined;
var main_err_msg_text_dom: i32 = undefined;

fn loadDomElements() void {
    queue_window_dom = dom.getElementById("queue-window");
    left_window_dom = dom.getElementById("left-window");
    now_playing_dom = dom.getElementById("nowplaying");
    main_err_msg_dom = dom.getElementById("main-err-msg");
    main_err_msg_text_dom = dom.getElementById("main-err-msg-text");
}

fn setLoadingState(state: LoadStatus) void {
    const show_ui = state == .good;
    dom.setShown(queue_window_dom, show_ui);
    dom.setShown(left_window_dom, show_ui);
    dom.setShown(now_playing_dom, show_ui);
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
