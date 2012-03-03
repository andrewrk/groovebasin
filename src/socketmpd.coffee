window.WEB_SOCKET_SWF_LOCATION = "/public/vendor/socket.io/WebSocketMain.swf"
window.SocketMpd = class SocketMpd extends window.Mpd
  constructor: (socket) ->
    super()
    @socket = socket
    @socket.on 'FromMpd', (data) =>
      @receive data
    @socket.on 'Initialize', (data) =>
      stuff = JSON.parse data.toString()
      @user_id = stuff.user_id
      @chats = stuff.chats
    @socket.on 'connect', @handleConnectionStart

  rawSend: (msg) =>
    @socket.emit 'ToMpd', msg
