
const wasmExports = require("wasmExports");

const decoder = new TextDecoder();
function decodeString(ptr, len) {
    return decoder.decode(new Uint8Array(wasmExports.memory.buffer, ptr, len));
}

const encoder = new TextEncoder();
function encodeString(s, dest) {
    const {read, written} = encoder.encodeInto(s, dest);
    if (read < s.length) throw new Error("dest too small");
    if (written < dest.length) throw new Error("dest too large");
}

return {
    decodeString,
    encodeString,
};