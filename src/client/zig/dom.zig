const env = @import("./browser_env.zig");

pub fn getElementById(id: []const u8) i32 {
    return env.getElementById(id.ptr, id.len);
}
pub const releaseElementHandle = env.releaseElementHandle;

pub fn setShown(handle: i32, shown: bool) void {
    env.setElementShown(handle, @boolToInt(shown));
}

pub fn setTextContent(handle: i32, text: []const u8) void {
    env.setElementTextContent(handle, text.ptr, text.len);
}

pub const getChildrenCount = env.getChildrenCount;
pub const getChild = env.getChild;

pub const InsertPosition = enum(i32) {
    beforebegin = 0,
    afterbegin = 1,
    beforeend = 2,
    afterend = 3,
};
pub fn insertAdjacentHTML(handle: i32, position: InsertPosition, html: []const u8) void {
    env.insertAdjacentHTML(handle, @enumToInt(position), html.ptr, html.len);
}
pub const removeLastChild = env.removeLastChild;

pub fn addClass(handle: i32, class: []const u8) void {
    env.addClass(handle, class.ptr, class.len);
}
pub fn removeClass(handle: i32, class: []const u8) void {
    env.removeClass(handle, class.ptr, class.len);
}
