const std = @import("std");
const Allocator = std.mem.Allocator;

const env = @import("browser_env.zig");
const callback = @import("callback.zig");
const browser = @import("browser.zig");

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
pub fn setAttribute(handle: i32, key: []const u8, value: []const u8) void {
    env.setAttribute(handle, key.ptr, key.len, value.ptr, value.len);
}
pub fn getAttribute(handle: i32, allocator: *Allocator, key: []const u8) []const u8 {
    const allocator_callback = callback.allocator(allocator);
    const packed_slice = env.getAttribute(
        handle,
        allocator_callback.callback,
        allocator_callback.context,
        key.ptr,
        key.len,
    );
    return browser.unpackSlice(packed_slice);
}

pub fn searchAncestorsForClass(start_handle: i32, stop_handle: i32, class: []const u8) i32 {
    return env.searchAncestorsForClass(start_handle, stop_handle, class.ptr, class.len);
}

pub const EventType = enum(i32) {
    // scraped from https://developer.mozilla.org/en-US/docs/Web/Events#event_listing
    // all event names are sorted and deduplicated.
    abort = 0,
    activate = 1,
    addstream = 2,
    addtrack = 3,
    afterprint = 4,
    afterscriptexecute = 5,
    animationcancel = 6,
    animationend = 7,
    animationiteration = 8,
    animationstart = 9,
    appinstalled = 10,
    audioend = 11,
    audioprocess = 12,
    audiostart = 13,
    auxclick = 14,
    beforeinput = 15,
    beforeprint = 16,
    beforescriptexecute = 17,
    beforeunload = 18,
    beginEvent = 19,
    blocked = 20,
    blur = 21,
    boundary = 22,
    bufferedamountlow = 23,
    cancel = 24,
    canplay = 25,
    canplaythrough = 26,
    change = 27,
    click = 28,
    close = 29,
    closing = 30,
    complete = 31,
    compositionend = 32,
    compositionstart = 33,
    compositionupdate = 34,
    connect = 35,
    connectionstatechange = 36,
    contentdelete = 37,
    contextmenu = 38,
    copy = 39,
    cuechange = 40,
    cut = 41,
    datachannel = 42,
    dblclick = 43,
    devicechange = 44,
    devicemotion = 45,
    deviceorientation = 46,
    DOMActivate = 47,
    DOMContentLoaded = 48,
    DOMMouseScroll = 49,
    drag = 50,
    dragend = 51,
    dragenter = 52,
    dragleave = 53,
    dragover = 54,
    dragstart = 55,
    drop = 56,
    durationchange = 57,
    emptied = 58,
    end = 59,
    ended = 60,
    endEvent = 61,
    enterpictureinpicture = 62,
    @"error" = 63,
    focus = 64,
    focusin = 65,
    focusout = 66,
    formdata = 67,
    fullscreenchange = 68,
    fullscreenerror = 69,
    gamepadconnected = 70,
    gamepaddisconnected = 71,
    gatheringstatechange = 72,
    gesturechange = 73,
    gestureend = 74,
    gesturestart = 75,
    gotpointercapture = 76,
    hashchange = 77,
    icecandidate = 78,
    icecandidateerror = 79,
    iceconnectionstatechange = 80,
    icegatheringstatechange = 81,
    input = 82,
    inputsourceschange = 83,
    install = 84,
    invalid = 85,
    keydown = 86,
    keypress = 87,
    keyup = 88,
    languagechange = 89,
    leavepictureinpicture = 90,
    load = 91,
    loadeddata = 92,
    loadedmetadata = 93,
    loadend = 94,
    loadstart = 95,
    lostpointercapture = 96,
    mark = 97,
    merchantvalidation = 98,
    message = 99,
    messageerror = 100,
    mousedown = 101,
    mouseenter = 102,
    mouseleave = 103,
    mousemove = 104,
    mouseout = 105,
    mouseover = 106,
    mouseup = 107,
    mousewheel = 108,
    msContentZoom = 109,
    MSGestureChange = 110,
    MSGestureEnd = 111,
    MSGestureHold = 112,
    MSGestureStart = 113,
    MSGestureTap = 114,
    MSInertiaStart = 115,
    MSManipulationStateChanged = 116,
    mute = 117,
    negotiationneeded = 118,
    nomatch = 119,
    notificationclick = 120,
    offline = 121,
    online = 122,
    open = 123,
    orientationchange = 124,
    pagehide = 125,
    pageshow = 126,
    paste = 127,
    pause = 128,
    payerdetailchange = 129,
    paymentmethodchange = 130,
    play = 131,
    playing = 132,
    pointercancel = 133,
    pointerdown = 134,
    pointerenter = 135,
    pointerleave = 136,
    pointerlockchange = 137,
    pointerlockerror = 138,
    pointermove = 139,
    pointerout = 140,
    pointerover = 141,
    pointerup = 142,
    popstate = 143,
    progress = 144,
    push = 145,
    pushsubscriptionchange = 146,
    ratechange = 147,
    readystatechange = 148,
    rejectionhandled = 149,
    removestream = 150,
    removetrack = 151,
    removeTrack = 152,
    repeatEvent = 153,
    reset = 154,
    resize = 155,
    resourcetimingbufferfull = 156,
    result = 157,
    @"resume" = 158,
    scroll = 159,
    search = 160,
    seeked = 161,
    seeking = 162,
    select = 163,
    selectedcandidatepairchange = 164,
    selectend = 165,
    selectionchange = 166,
    selectstart = 167,
    shippingaddresschange = 168,
    shippingoptionchange = 169,
    show = 170,
    signalingstatechange = 171,
    slotchange = 172,
    soundend = 173,
    soundstart = 174,
    speechend = 175,
    speechstart = 176,
    squeeze = 177,
    squeezeend = 178,
    squeezestart = 179,
    stalled = 180,
    start = 181,
    statechange = 182,
    storage = 183,
    submit = 184,
    success = 185,
    @"suspend" = 186,
    timeout = 187,
    timeupdate = 188,
    toggle = 189,
    tonechange = 190,
    touchcancel = 191,
    touchend = 192,
    touchmove = 193,
    touchstart = 194,
    track = 195,
    transitioncancel = 196,
    transitionend = 197,
    transitionrun = 198,
    transitionstart = 199,
    unhandledrejection = 200,
    unload = 201,
    unmute = 202,
    upgradeneeded = 203,
    versionchange = 204,
    visibilitychange = 205,
    voiceschanged = 206,
    volumechange = 207,
    vrdisplayactivate = 208,
    vrdisplayblur = 209,
    vrdisplayconnect = 210,
    vrdisplaydeactivate = 211,
    vrdisplaydisconnect = 212,
    vrdisplayfocus = 213,
    vrdisplaypointerrestricted = 214,
    vrdisplaypointerunrestricted = 215,
    vrdisplaypresentchange = 216,
    waiting = 217,
    webglcontextcreationerror = 218,
    webglcontextlost = 219,
    webglcontextrestored = 220,
    webkitmouseforcechanged = 221,
    webkitmouseforcedown = 222,
    webkitmouseforceup = 223,
    webkitmouseforcewillbegin = 224,
    wheel = 225,
};
pub fn addEventListener(handle: i32, event_type: EventType, cb: *const callback.CallbackFnI32, context: *callback.Context) void {
    env.addEventListener(handle, @enumToInt(event_type), cb, context);
}

pub const getEventTarget = env.getEventTarget;

pub const setInputValueAsNumber = env.setInputValueAsNumber;
pub const getInputValueAsNumber = env.getInputValueAsNumber;
