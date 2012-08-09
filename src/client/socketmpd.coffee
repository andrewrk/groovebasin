#depend "mpd" bare

window.SocketMpd = class SocketMpd extends window.Mpd
  constructor: (@socket) ->
    super()
    @socket.on 'FromMpd', => @receive.apply(this, arguments)
    @socket.on 'MpdConnect', => @handleConnectionStart.apply(this, arguments)
    @socket.on 'MpdDisconnect', => @resetServerState.apply(this, arguments)
    @socket.on 'disconnect', => @resetServerState.apply(this, arguments)

  rawSend: (msg) =>
    @socket.emit 'ToMpd', msg
