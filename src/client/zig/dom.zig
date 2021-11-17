const env = @import("./browser_env.zig");

pub fn getElementById(id: []const u8) i32 {
    return env.getElementById(id.ptr, id.len);
}

pub fn setShown(handle: i32, shown: bool) void {
    env.setElementShown(handle, @boolToInt(shown));
}

pub fn setTextContent(handle: i32, text: []const u8) void {
    env.setElementTextContent(handle, text.ptr, text.len);
}
