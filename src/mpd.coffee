window.WEB_SOCKET_SWF_LOCATION = "/public/vendor/socket.io/WebSocketMain.swf"
class Mpd
  MPD_INIT = /^OK MPD .+\n$/
  MPD_SENTINEL = /OK\n$/
  MPD_ACK = /^ACK \[\d+@\d+\].*\n$/

  constructor: ->
    @socket = io.connect "http://localhost"
    @buffer = ""

    # queue of callbacks to call when we get data from mpd, indexed by command
    @callbacks = {}
    # what kind of response to expect back
    @expectStack = []

    @socket.on 'FromMpd', (data) =>
      @buffer += data

      if MPD_INIT.test @buffer
        @buffer = ""
      else if MPD_ACK.test @buffer
        @onError @buffer
        @buffer = ""
      else if MPD_SENTINEL.test @buffer
        @onMessage @buffer.substring(0, @buffer.length-3)
        @buffer = ""
  
  onError: (msg) ->
    alert "error: " + msg

  onMessage: (msg) ->
    $("#text").val(msg)

    cmd = @expectStack.pop()
    callbacks = @callbacks[cmd]
    @callbacks[cmd] = []
    switch cmd
      when 'list artist'
        # remove the 'Artist: ' text from every line and convert to array
        cb (line.substring(8) for line in msg.split('\n')) for cb in callbacks
      else
        alert "unhandled command: " + cmd

  send: (msg) ->
    @socket.emit 'ToMpd', msg + "\n"

  pushSend: (command, callback) ->
    @callbacks[command] ||= []
    @callbacks[command].push callback
    if $.inArray(command, @expectStack) == -1
      @expectStack.push command
      @send command

  getArtistList: (callback) ->
    @pushSend 'list artist', callback
