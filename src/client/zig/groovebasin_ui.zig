const dom = @import("dom.zig");

pub const LoadStatus = enum {
    init,
    no_connection,
    good,
};

var main_grid_dom: i32 = undefined;
var main_err_msg_dom: i32 = undefined;
var main_err_msg_text_dom: i32 = undefined;

pub fn loadDomElements() void {
    main_grid_dom = dom.getElementById("main-grid");
    main_err_msg_dom = dom.getElementById("main-err-msg");
    main_err_msg_text_dom = dom.getElementById("main-err-msg-text");
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
