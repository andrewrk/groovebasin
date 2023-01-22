// These must match the enums in browser_enums.zig.

const PositionType = [
    "beforebegin",
    "afterbegin",
    "beforeend",
    "afterend",
];

const EventType = require("_generated_EventType");
const KeyboardEventCode = require("_generated_KeyboardEventCode");

const KeyboardEventCode_inverse = {};
for (let i = 0; i < KeyboardEventCode.length; i++) {
    KeyboardEventCode_inverse[KeyboardEventCode[i]] = i;
}
// When Firefox uses "Unidentified", Chrome uses "".
KeyboardEventCode_inverse[""] = KeyboardEventCode_inverse["Unidentified"];

return {
    PositionType,
    EventType,
    KeyboardEventCode,
    KeyboardEventCode_inverse,
};
