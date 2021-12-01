const {HandleRegistry} = require("handleRegistry");
const wasmExports = require("wasmExports");

const wsUrl = (() => {
    var host = window.document.location.host;
    var pathname = window.document.location.pathname;
    var isHttps = window.document.location.protocol === 'https:';
    var match = host.match(/^(.+):(\d+)$/);
    var defaultPort = isHttps ? 443 : 80;
    var port = match ? parseInt(match[2], 10) : defaultPort;
    var hostName = match ? match[1] : host;
    var wsProto = isHttps ? "wss:" : "ws:";
    var wsUrl = wsProto + '//' + hostName + ':' + port + pathname;
    return wsUrl;
})();

const wsRegistry = new HandleRegistry();

function openWebSocket(allocatorCallback, openCallback, closeCallback, errorCallback, messageCallback) {
    const ws = new WebSocket(wsUrl);
    const {handle, dispose} = wsRegistry.alloc(ws);
    ws.binaryType = "arraybuffer";

    ws.addEventListener("open", onOpen);
    ws.addEventListener("close", onClose);
    ws.addEventListener("error", onError);
    ws.addEventListener("message", onMessage);

    function cleanup() {
        ws.removeEventListener("open", onOpen);
        ws.removeEventListener("close", onClose);
        ws.removeEventListener("error", onError);
        ws.removeEventListener("message", onMessage);
        dispose();
    }

    function onOpen() {
        console.log("JS: websocket open");
        openCallback(handle);
    }

    function onClose(ev) {
        console.log("JS: websocket close:", ev);
        cleanup();
        closeCallback(ev.code);
    }

    function onError(ev) {
        console.log("JS: websocket error:", ev);
        cleanup();
        errorCallback();
    }

    function onMessage(ev) {
        const jsArray = new Uint8Array(ev.data);
        const len = jsArray.length;
        const ptr = allocatorCallback(len);
        const wasmArray = new Uint8Array(wasmExports.memory.buffer, ptr, len);
        wasmArray.set(jsArray);
        messageCallback(ptr, len);
    }
}

function sendMessage(wsHandle, buf) {
    const ws = wsRegistry.registry[wsHandle];
    if (ws == null) throw new Error("bad ws handle");
    ws.send(buf);
}

return {
    openWebSocket,
    sendMessage,
};
