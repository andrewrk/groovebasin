
const memory = require("memory");

const decoder = new TextDecoder();
function decodeString(ptr, len) {
    return decoder.decode(new Uint8Array(memory.buffer, ptr, len));
}

return {
    decodeString,
};