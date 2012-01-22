window.WEB_SOCKET_SWF_LOCATION = "/public/vendor/socket.io/WebSocketMain.swf"
class Mpd
  MPD_INIT = /^OK MPD .+\n$/
  MPD_SENTINEL = /OK\n$/
  MPD_ACK = /^ACK \[\d+@\d+\].*\n$/

  startsWith = (string, str) -> string.substring(0, str.length) == str
  stripPrefixes = ['the ', 'a ', 'an ']
  sortableTitle = (title) ->
    t = title.toLowerCase()
    for prefix in stripPrefixes
      if startsWith(t, prefix)
        t = t.substring(prefix.length)
        break
    t

  titleSort = (a,b) ->
    _a = sortableTitle(a)
    _b = sortableTitle(b)
    if _a < _b
      -1
    else if _a > _b
      1
    else
      0

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

  onListArtistMsg = (msg) ->
    # remove the 'Artist: ' text from every line and convert to array
    list = (line.substring(8) for line in msg.split('\n'))
    list.sort titleSort
    list

  onFindArtistMsg = (msg) ->
    # build list of tracks from msg
    tracks = {}
    current_file = null
    for line in msg.split("\n")
      [key, value] = line.split(": ")
      if key == 'file'
        current_file = value
        tracks[current_file] = {}
      tracks[current_file][key] = value

    # generate list of albums
    albums = {}
    for file, track of tracks
      album_name = track.Album ? "Unknown Album"
      albums[album_name] ||= []
      albums[album_name].push track

    albums

  onSendCommandMsg = (msg) -> msg

  doNothing = ->

  onMessage: (msg) ->
    [cmd, handler] = @expectStack.pop()
    cb handler(msg) for cb in @callbacks[cmd]
    @callbacks[cmd] = []

  send: (msg) ->
    @socket.emit 'ToMpd', msg + "\n"

  pushSend: (command, handler, callback) ->
    @callbacks[command] ||= []
    @callbacks[command].push callback
    if $.inArray(command, @expectStack) == -1
      @expectStack.push [command, handler]
      @send command

  getArtistList: (callback) ->
    @pushSend 'list artist', onListArtistMsg, callback

  getAlbumsForArtist: (artist_name, callback) ->
    @pushSend "find artist \"#{artist_name}\"", onFindArtistMsg, callback

  sendCommand: (cmd, callback) ->
    @pushSend cmd, onSendCommandMsg, callback

  queueTrack: (file) ->
    @pushSend "add \"#{file}\"", doNothing, doNothing
