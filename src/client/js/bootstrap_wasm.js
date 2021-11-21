const wasmExports = require("wasmExports");

const {decodeString, encodeString} = require("string");
const {openWebSocket, sendMessage} = require("websocket");
const {readBlob} = require("blob");
const callback = require("callback");
const dom = require("dom");

const PositionType = [
    'beforebegin',
    'afterbegin',
    'beforeend',
    'afterend',
];

const EventType = [
    'abort',
    'activate',
    'addstream',
    'addtrack',
    'afterprint',
    'afterscriptexecute',
    'animationcancel',
    'animationend',
    'animationiteration',
    'animationstart',
    'appinstalled',
    'audioend',
    'audioprocess',
    'audiostart',
    'auxclick',
    'beforeinput',
    'beforeprint',
    'beforescriptexecute',
    'beforeunload',
    'beginEvent',
    'blocked',
    'blur',
    'boundary',
    'bufferedamountlow',
    'cancel',
    'canplay',
    'canplaythrough',
    'change',
    'click',
    'close',
    'closing',
    'complete',
    'compositionend',
    'compositionstart',
    'compositionupdate',
    'connect',
    'connectionstatechange',
    'contentdelete',
    'contextmenu',
    'copy',
    'cuechange',
    'cut',
    'datachannel',
    'dblclick',
    'devicechange',
    'devicemotion',
    'deviceorientation',
    'DOMActivate',
    'DOMContentLoaded',
    'DOMMouseScroll',
    'drag',
    'dragend',
    'dragenter',
    'dragleave',
    'dragover',
    'dragstart',
    'drop',
    'durationchange',
    'emptied',
    'end',
    'ended',
    'endEvent',
    'enterpictureinpicture',
    'error',
    'focus',
    'focusin',
    'focusout',
    'formdata',
    'fullscreenchange',
    'fullscreenerror',
    'gamepadconnected',
    'gamepaddisconnected',
    'gatheringstatechange',
    'gesturechange',
    'gestureend',
    'gesturestart',
    'gotpointercapture',
    'hashchange',
    'icecandidate',
    'icecandidateerror',
    'iceconnectionstatechange',
    'icegatheringstatechange',
    'input',
    'inputsourceschange',
    'install',
    'invalid',
    'keydown',
    'keypress',
    'keyup',
    'languagechange',
    'leavepictureinpicture',
    'load',
    'loadeddata',
    'loadedmetadata',
    'loadend',
    'loadstart',
    'lostpointercapture',
    'mark',
    'merchantvalidation',
    'message',
    'messageerror',
    'mousedown',
    'mouseenter',
    'mouseleave',
    'mousemove',
    'mouseout',
    'mouseover',
    'mouseup',
    'mousewheel',
    'msContentZoom',
    'MSGestureChange',
    'MSGestureEnd',
    'MSGestureHold',
    'MSGestureStart',
    'MSGestureTap',
    'MSInertiaStart',
    'MSManipulationStateChanged',
    'mute',
    'negotiationneeded',
    'nomatch',
    'notificationclick',
    'offline',
    'online',
    'open',
    'orientationchange',
    'pagehide',
    'pageshow',
    'paste',
    'pause',
    'payerdetailchange',
    'paymentmethodchange',
    'play',
    'playing',
    'pointercancel',
    'pointerdown',
    'pointerenter',
    'pointerleave',
    'pointerlockchange',
    'pointerlockerror',
    'pointermove',
    'pointerout',
    'pointerover',
    'pointerup',
    'popstate',
    'progress',
    'push',
    'pushsubscriptionchange',
    'ratechange',
    'readystatechange',
    'rejectionhandled',
    'removestream',
    'removetrack',
    'removeTrack',
    'repeatEvent',
    'reset',
    'resize',
    'resourcetimingbufferfull',
    'result',
    'resume',
    'scroll',
    'search',
    'seeked',
    'seeking',
    'select',
    'selectedcandidatepairchange',
    'selectend',
    'selectionchange',
    'selectstart',
    'shippingaddresschange',
    'shippingoptionchange',
    'show',
    'signalingstatechange',
    'slotchange',
    'soundend',
    'soundstart',
    'speechend',
    'speechstart',
    'squeeze',
    'squeezeend',
    'squeezestart',
    'stalled',
    'start',
    'statechange',
    'storage',
    'submit',
    'success',
    'suspend',
    'timeout',
    'timeupdate',
    'toggle',
    'tonechange',
    'touchcancel',
    'touchend',
    'touchmove',
    'touchstart',
    'track',
    'transitioncancel',
    'transitionend',
    'transitionrun',
    'transitionstart',
    'unhandledrejection',
    'unload',
    'unmute',
    'upgradeneeded',
    'versionchange',
    'visibilitychange',
    'voiceschanged',
    'volumechange',
    'vrdisplayactivate',
    'vrdisplayblur',
    'vrdisplayconnect',
    'vrdisplaydeactivate',
    'vrdisplaydisconnect',
    'vrdisplayfocus',
    'vrdisplaypointerrestricted',
    'vrdisplaypointerunrestricted',
    'vrdisplaypresentchange',
    'waiting',
    'webglcontextcreationerror',
    'webglcontextlost',
    'webglcontextrestored',
    'webkitmouseforcechanged',
    'webkitmouseforcedown',
    'webkitmouseforceup',
    'webkitmouseforcewillbegin',
    'wheel',
];

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
    getTime() {
        return BigInt(new Date().getTime());
    },
    setTimeout(callbackPtr, context, timeout) {
        return BigInt(setTimeout(callback.wrapCallback(callbackPtr, context), timeout));
    },
    setInterval(callbackPtr, context, timeout) {
        return BigInt(setInterval(callback.wrapCallback(callbackPtr, context), timeout));
    },
    clearTimer(handle) {
        clearTimeout(Number(handle));
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
    readAttribute(handle, key_ptr, key_len, buf_ptr, buf_len) {
        const key = decodeString(key_ptr, key_len);
        const dest = new Uint8Array(wasmExports.memory.buffer, buf_ptr, buf_len);
        const value = dom.getAttribute(handle, key);
        return encodeString(value, dest);
    },
    searchAncestorsForClass(start_handle, stop_handle, class_ptr, class_len) {
        const class_ = decodeString(class_ptr, class_len);
        return dom.searchAncestorsForClass(start_handle, stop_handle, class_);
    },
    addEventListener(handle, event_type, callbackPtr, context) {
        return dom.addEventListener(handle, EventType[event_type], callback.wrapCallbackI32(callbackPtr, context));
    },
    getEventTarget(handle) {
        return dom.getEventTarget(handle);
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
