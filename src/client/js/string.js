
const wasmExports = require("wasmExports");

const decoder = new TextDecoder();
function decodeString(ptr, len) {
    return decoder.decode(new Uint8Array(wasmExports.memory.buffer, ptr, len));
}

const encoder = new TextEncoder();
function encodeStringAlloc(allocatorCallback, s) {
    if (s.length === 0) return 0n;
    const jsArray = encoder.encode(s);
    const len = jsArray.length;
    const ptr = allocatorCallback(len);
    const wasmArray = new Uint8Array(wasmExports.memory.buffer, ptr, len);
    wasmArray.set(jsArray);
    return packSlice(ptr, len);
}

function packSlice(ptr, len) {
    // We need to return struct{u32,u32} from some functions.
    // Just pack it all into an i64, easy.
    return (BigInt(ptr | 0) << 32n) | BigInt(len >>> 0);
}

return {
    decodeString,
    encodeStringAlloc,
};
