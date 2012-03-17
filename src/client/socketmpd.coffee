window.WEB_SOCKET_SWF_LOCATION = "/public/vendor/socket.io/WebSocketMain.swf"
window.SocketMpd = class SocketMpd extends window.Mpd
  constructor: (@socket) ->
    super()
    @socket.on 'FromMpd', @receive
    @socket.on 'MpdConnect', @handleConnectionStart
    @socket.on 'MpdDisconnect', @resetServerState
    @socket.on 'disconnect', @resetServerState

  rawSend: (msg) =>
    @socket.emit 'ToMpd', msg
