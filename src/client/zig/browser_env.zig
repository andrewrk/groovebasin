// This is the {env} passed into the wasm instantiation.
// See also browser.zig, dom.zig, etc. for more conveient altnernatives for some of these.

const enums = @import("browser_enums.zig");
const InsertPosition = enums.InsertPosition;
const EventType = enums.EventType;
const EventModifiers = enums.EventModifiers;

// Essentials
pub extern fn print(ptr: [*]const u8, len: usize) void;
pub extern fn panic(ptr: [*]const u8, len: usize) void;
pub extern fn getTime() i64;
pub extern fn setTimeout(
    cb: i64, //callback.Callback,
    milliseconds: i32,
) i64;
pub extern fn setInterval(
    cb: i64, //callback.Callback,
    milliseconds: i32,
) i64;
pub extern fn clearTimer(handle: i64) void;

// WebSocket API
pub extern fn openWebSocket(
    allocatorCallback: i64, //callback.CallbackI32RI32,
    openCallback: i64, //callback.CallbackI32,
    closeCallback: i64, //callback.CallbackI32,
    errorCallback: i64, //callback.Callback,
    messageCallback: i64, //callback.CallbackSliceU8,
) void;
pub extern fn sendMessage(handle: i32, ptr: [*]const u8, len: usize) void;

// Dom
pub extern fn getElementById(ptr: [*]const u8, len: usize) i32;
pub extern fn setElementShown(handle: i32, shown: i32) void;
pub extern fn setElementTextContent(handle: i32, ptr: [*]const u8, len: usize) void;
pub extern fn getChildrenCount(handle: i32) i32;
pub extern fn getChild(handle: i32, i: i32) i32;
pub extern fn insertAdjacentHTML(handle: i32, position: InsertPosition, html_ptr: [*]const u8, html_len: usize) void;
pub extern fn removeLastChild(handle: i32) void;
pub extern fn addClass(handle: i32, class_ptr: [*]const u8, class_len: usize) void;
pub extern fn removeClass(handle: i32, class_ptr: [*]const u8, class_len: usize) void;
pub extern fn setAttribute(handle: i32, key_ptr: [*]const u8, key_len: usize, value_ptr: [*]const u8, value_len: usize) void;
pub extern fn getAttribute(
    handle: i32,
    allocatorCallback: i64, //callback.CallbackI32RI32,
    key_ptr: [*]const u8,
    key_len: usize,
) i64;
pub extern fn searchAncestorsForClass(start_handle: i32, stop_handle: i32, class_ptr: [*]const u8, class_len: usize) i32;
pub extern fn addEventListener(
    handle: i32,
    event_type: EventType,
    cb: i64, //callback.CallbackI32,
) void;
pub extern fn getEventTarget(handle: i32) i32;
pub extern fn getEventModifiers(handle: i32) EventModifiers;
pub extern fn preventDefault(handle: i32) void;
pub extern fn setInputValueAsNumber(handle: i32, value: f64) void;
pub extern fn getInputValueAsNumber(handle: i32) f64;

// Audio
pub extern fn newAudio() i32;
pub extern fn setAudioSrc(handle: i32, src_ptr: [*]const u8, src_len: usize) void;
pub extern fn loadAudio(handle: i32) void;
pub extern fn playAudio(handle: i32) void;
pub extern fn pauseAudio(handle: i32) void;
pub extern fn setAudioVolume(handle: i32, volume: f64) void;
