const wasmExports = require("wasmExports");

const {decodeString} = require("string");
const {openWebSocket, sendMessage} = require("websocket");
const {readBlob} = require("blob");
const callback = require("callback");
const dom = require("dom");

const env = {
    // Essentials
    print(ptr, len) {
        const msg = decodeString(ptr, len);
        console.log(msg);
    },
    panic(ptr, len) {
        const msg = decodeString(ptr, len);
        throw new Error("panic: " + msg);
    },
    readBlob(handle, ptr, len) {
        const dest = new Uint8Array(wasmExports.memory.buffer, ptr, len);
        readBlob(handle, dest);
    },
    setTimeout(callbackPtr, context, timeout) {
        setTimeout(callback.wrapCallback(callbackPtr, context), timeout);
    },

    // WebSocket API
    openWebSocket(
        openCallbackPtr, openCallbackContext,
        closeCallbackPtr, closeCallbackContext,
        errorCallbackPtr, errorCallbackContext,
        messageCallbackPtr, messageCallbackContext,
    ) {
        openWebSocket(
            callback.wrapCallbackI32(openCallbackPtr, openCallbackContext),
            callback.wrapCallbackI32(closeCallbackPtr, closeCallbackContext),
            callback.wrapCallback(errorCallbackPtr, errorCallbackContext),
            callback.wrapCallbackI32I32(messageCallbackPtr, messageCallbackContext),
        );
    },
    sendMessage(handle, ptr, len) {
        const buf = new Uint8Array(wasmExports.memory.buffer, ptr, len);
        sendMessage(handle, buf);
    },

    // Dom
    getElementById(ptr, len) {
        const id = decodeString(ptr, len);
        return dom.getElementById(id);
    },
    releaseElementHandle: dom.releaseElementHandle,
    setElementShown(handle, shown) {
        return dom.setElementShown(handle, !!shown);
    },
    setElementTextContent(handle, ptr, len) {
        const text = decodeString(ptr, len);
        return dom.setElementTextContent(handle, text);
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
