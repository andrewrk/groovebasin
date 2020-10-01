
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

function serveWebSocket(openCallback, closeCallbackI32, errorCallback, messageCallback) {
    const ws = new WebSocket(wsUrl);

    ws.addEventListener("open", onOpen);
    ws.addEventListener("close", onClose);
    // ws.addEventListener("error", onError);
    // ws.addEventListener("message", onMessage);

    function cleanup() {
        ws.removeEventListener("open", onOpen);
        ws.removeEventListener("close", onClose);
        // ws.removeEventListener("error", onError);
        // ws.removeEventListener("message", onMessage);
    }

    function onOpen() {
        console.log("websocket open");
        openCallback();
    }

    function onClose(ev) {
        console.log("websocket close:", ev);
        cleanup();
        closeCallbackI32(ev.code);
    }

    function onError(ev) {
        console.log("websocket error:", ev);
        cleanup();
        errorCallback();
    }

    function onMessage(ev) {
        console.log("websocket message:", ev.data);
        messageCallback(ev.data);
    }
}

return {
    serveWebSocket,
};
