
const wasmExports = require("wasmExports");

const decoder = new TextDecoder();
function decodeString(ptr, len) {
    return decoder.decode(new Uint8Array(wasmExports.memory.buffer, ptr, len));
}

return {
    decodeString,
};