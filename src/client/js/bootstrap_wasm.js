const wasmExports = require("wasmExports");

const {decodeString, encodeStringAlloc} = require("string");
const {openWebSocket, sendMessage} = require("websocket");
const callback = require("callback");
const dom = require("dom");
const audio = require("audio");
const {PositionType, EventType} = require("enums");

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
    getTime() {
        return BigInt(new Date().getTime());
    },
    setTimeout(cb, timeout) {
        return BigInt(setTimeout(callback.wrapCallback(cb), timeout));
    },
    setInterval(cb, timeout) {
        return BigInt(setInterval(callback.wrapCallback(cb), timeout));
    },
    clearTimer(handle) {
        clearTimeout(Number(handle));
    },

    // WebSocket API
    openWebSocket(
        allocatorCallback,
        openCallback,
        closeCallback,
        errorCallback,
        messageCallback,
    ) {
        openWebSocket(
            callback.wrapCallbackI32RI32(allocatorCallback),
            callback.wrapCallbackI32(openCallback),
            callback.wrapCallbackI32(closeCallback),
            callback.wrapCallback(errorCallback),
            callback.wrapCallbackSliceU8(messageCallback),
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
    setElementShown(handle, shown) {
        return dom.setElementShown(handle, !!shown);
    },
    setElementTextContent(handle, ptr, len) {
        const text = decodeString(ptr, len);
        return dom.setElementTextContent(handle, text);
    },
    getChildrenCount(handle) {
        return dom.getChildrenCount(handle);
    },
    getChild(handle, i) {
        return dom.getChild(handle, i);
    },
    insertAdjacentHTML(handle, position_int, html_ptr, html_len) {
        const position = PositionType[position_int];
        const html = decodeString(html_ptr, html_len);
        return dom.insertAdjacentHTML(handle, position, html);
    },
    removeLastChild(handle) {
        return dom.removeLastChild(handle);
    },
    addClass(handle, class_ptr, class_len) {
        const class_ = decodeString(class_ptr, class_len);
        return dom.addClass(handle, class_);
    },
    removeClass(handle, class_ptr, class_len) {
        const class_ = decodeString(class_ptr, class_len);
        return dom.removeClass(handle, class_);
    },
    setAttribute(handle, key_ptr, key_len, value_ptr, value_len) {
        const key = decodeString(key_ptr, key_len);
        const value = decodeString(value_ptr, value_len);
        return dom.setAttribute(handle, key, value);
    },
    getAttribute(handle, allocatorCallback,key_ptr, key_len) {
        const key = decodeString(key_ptr, key_len);
        const value = dom.getAttribute(handle, key);
        return encodeStringAlloc(callback.wrapCallbackI32RI32(allocatorCallback), value);
    },
    searchAncestorsForClass(start_handle, stop_handle, class_ptr, class_len) {
        const class_ = decodeString(class_ptr, class_len);
        return dom.searchAncestorsForClass(start_handle, stop_handle, class_);
    },
    addEventListener(handle, event_type, cb) {
        return dom.addEventListener(handle, EventType[event_type], callback.wrapCallbackI32(cb));
    },
    addWindowEventListener(event_type, cb) {
        return dom.addWindowEventListener(EventType[event_type], callback.wrapCallbackI32(cb));
    },
    getEventTarget(handle) {
        return dom.getEventTarget(handle);
    },
    getEventModifiers(handle) {
        return dom.getEventModifiers(handle);
    },
    getKeyboardEventCode(handle) {
        return dom.getKeyboardEventCode(handle);
    },
    preventDefault(handle) {
        return dom.preventDefault(handle);
    },
    setInputValueAsNumber(handle, value) {
        return dom.setInputValueAsNumber(handle, value);
    },
    getInputValueAsNumber(handle) {
        return dom.getInputValueAsNumber(handle);
    },

    // Audio
    newAudio() {
        return audio.newAudio();
    },
    setAudioSrc(handle, src_ptr, src_len) {
        const src = decodeString(src_ptr, src_len);
        return audio.setAudioSrc(handle, src);
    },
    loadAudio(handle) {
        return audio.loadAudio(handle);
    },
    playAudio(handle) {
        return audio.playAudio(handle);
    },
    pauseAudio(handle) {
        return audio.pauseAudio(handle);
    },
    setAudioVolume(handle, volume) {
        return audio.setAudioVolume(handle, volume);
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
