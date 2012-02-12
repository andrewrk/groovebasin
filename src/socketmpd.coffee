window.WEB_SOCKET_SWF_LOCATION = "/public/vendor/socket.io/WebSocketMain.swf"
window.SocketMpd = class _ extends window.Mpd
  constructor: ->
    super()
    @socket = io.connect(undefined, {'force new connection': true})
    @socket.on 'FromMpd', (data) =>
      @receive data
    @socket.on 'connect', @handleConnectionStart

  send: (msg) =>
    @debugMsgConsole?.log "send: #{@msgHandlerQueue[@msgHandlerQueue.length - 1]?.debug_id ? -1}: " + JSON.stringify(msg)
    @socket.emit 'ToMpd', msg
