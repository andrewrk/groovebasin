const {HandleRegistry} = require("handleRegistry");
const {createBlob} = require("blob");

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

function serveWebSocket(openCallback, closeCallback, errorCallback, messageCallback) {
    const ws = new WebSocket(wsUrl);
    const {handle, dispose} = wsRegistry.alloc(ws);

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
        console.log("websocket open");
        openCallback(handle);
    }

    function onClose(ev) {
        console.log("websocket close:", ev);
        cleanup();
        closeCallback(ev.code);
    }

    function onError(ev) {
        console.log("websocket error:", ev);
        cleanup();
        errorCallback();
    }

    async function onMessage(ev) {
        const array = new Uint8Array(await ev.data.arrayBuffer());
        const {handle, dispose} = createBlob(array);
        try {
            messageCallback(handle, array.length);
        } finally {
            dispose();
        }
    }
}

function sendMessage(wsHandle, buf) {
    const ws = wsRegistry.registry[wsHandle];
    if (ws == null) throw new Error("bad ws handle");
    ws.send(buf);
}

return {
    serveWebSocket,
    sendMessage,
};
