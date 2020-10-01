const memory = require("memory");

const _decoder = new TextDecoder();
const {decodeString} = require("string");

const env = {
    print: (ptr, len) => {
        const msg = decodeString(ptr, len);
        console.log(msg);
    },
};

(async () => {
    const {instance} = await WebAssembly.instantiateStreaming(fetch('client.wasm'), {env});
    window._wasm = instance;
    memory.buffer = instance.exports.memory.buffer;
    instance.exports.main();
})();
