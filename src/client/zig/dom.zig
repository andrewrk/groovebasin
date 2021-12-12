const std = @import("std");
const Allocator = std.mem.Allocator;

const env = @import("browser_env.zig");
const callback = @import("callback.zig");
const browser = @import("browser.zig");
const enums = @import("browser_enums.zig");
const InsertPosition = enums.InsertPosition;
const EventType = enums.EventType;

pub fn getElementById(id: []const u8) i32 {
    return env.getElementById(id.ptr, id.len);
}

pub fn setShown(handle: i32, shown: bool) void {
    env.setElementShown(handle, @boolToInt(shown));
}

pub fn setTextContent(handle: i32, text: []const u8) void {
    env.setElementTextContent(handle, text.ptr, text.len);
}

pub const getChildrenCount = env.getChildrenCount;
pub const getChild = env.getChild;

pub fn insertAdjacentHTML(handle: i32, position: InsertPosition, html: []const u8) void {
    env.insertAdjacentHTML(handle, position, html.ptr, html.len);
}
pub const removeLastChild = env.removeLastChild;

pub fn addClass(handle: i32, class: []const u8) void {
    env.addClass(handle, class.ptr, class.len);
}
pub fn removeClass(handle: i32, class: []const u8) void {
    env.removeClass(handle, class.ptr, class.len);
}
pub fn setAttribute(handle: i32, key: []const u8, value: []const u8) void {
    env.setAttribute(handle, key.ptr, key.len, value.ptr, value.len);
}
pub fn getAttribute(handle: i32, allocator: *Allocator, key: []const u8) []u8 {
    const packed_slice = env.getAttribute(
        handle,
        callback.allocator(allocator).handle,
        key.ptr,
        key.len,
    );
    return browser.unpackSlice(packed_slice);
}

pub fn searchAncestorsForClass(start_handle: i32, stop_handle: i32, class: []const u8) i32 {
    return env.searchAncestorsForClass(start_handle, stop_handle, class.ptr, class.len);
}

pub fn addEventListener(
    handle: i32,
    event_type: EventType,
    cb: callback.CallbackI32,
) void {
    return env.addEventListener(handle, event_type, cb.handle);
}
pub fn addWindowEventListener(
    event_type: EventType,
    cb: callback.CallbackI32,
) void {
    return env.addWindowEventListener(event_type, cb.handle);
}
pub const getEventTarget = env.getEventTarget;
pub const getEventModifiers = env.getEventModifiers;
pub const getKeyboardEventCode = env.getKeyboardEventCode;
pub const preventDefault = env.preventDefault;
pub const stopPropagation = env.stopPropagation;

pub fn setInputValue(handle: i32, value: []const u8) void {
    return env.setInputValue(handle, value.ptr, value.len);
}
pub fn getInputValue(handle: i32, allocator: *Allocator) []u8 {
    const packed_slice = env.getInputValue(
        handle,
        callback.allocator(allocator).handle,
    );
    return browser.unpackSlice(packed_slice);
}
pub const setInputValueAsNumber = env.setInputValueAsNumber;
pub const getInputValueAsNumber = env.getInputValueAsNumber;

pub const focus = env.focus;
pub const blur = env.blur;
