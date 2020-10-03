const wasmExports = require("wasmExports");

const {decodeString} = require("string");
const {serveWebSocket} = require("websocket");
const callback = require("callback");

const env = {
    print(ptr, len) {
        const msg = decodeString(ptr, len);
        console.log(msg);
    },
    serveWebSocket(
        openCallbackPtr, openCallbackContext,
        closeCallbackPtr, closeCallbackContext,
        // errorCallbackPtr, errorCallbackContext,
        // messageCallbackPtr, messageCallbackContext,
    ) {
        serveWebSocket(
            callback.wrapCallback(openCallbackPtr, openCallbackContext),
            callback.wrapCallbackI32(closeCallbackPtr, closeCallbackContext),
            // null,
            // null,
        );
    },
};

(async () => {
    const {instance} = await WebAssembly.instantiateStreaming(fetch('client.wasm'), {env});
    // for debugging
    window._wasm = instance;

    // Expose exports.
    for (const name in instance.exports) {
        if (name === "main") continue;
        wasmExports[name] = instance.exports[name];
    }

    // main
    instance.exports.main();
})();
