const browser = @import("./browser_env.zig");

pub fn getElementById(id: []const u8) i32 {
    return browser.getElementById(id.ptr, id.len);
}

pub fn setShown(handle: i32, shown: bool) void {
    browser.setElementShown(handle, @boolToInt(shown));
}

pub fn setTextContent(handle: i32, text: []const u8) void {
    browser.setElementTextContent(handle, text.ptr, text.len);
}
