# library structure: {
#   artists: [sorted list of {artist structure}],
#   track_table: {"track file" => {track structure}},
# }
# artist structure: {
#   name: "Artist Name",
#   albums: [sorted list of {album structure}],
# }
# album structure:  {
#   name: "Album Name",
#   year: 1999,
#   tracks: [sorted list of {track structure}],
# }
# track structure: {
#   name: "Track Name",
#   track: 9,
#   artist: {artist structure},
#   album: {album structure},
#   file: "Obtuse/Cloudy Sky/06. Temple of Trance.mp3",
#   time: 263, # length in seconds
# }
# playlist structure: {
#   item_list: [sorted list of {playlist item structure}],
#   item_table: {song id => {playlist item structure}}
# }
# playlist item structure: {
#   id: 7, # playlist song id
#   track: {track structure},
# }
# status structure: {
#   volume: .89, # float 0-1
#   repeat: true, # whether we're in repeat mode. see also `single`
#   random: false, # random mode makes the next song random within the playlist
#   single: true, # true -> repeat the current song, false -> repeat the playlist
#   consume: true, # true -> remove tracks from playlist after done playing
#   state: "play", # or "stop" or "pause"
#   time: 234, # length of song in seconds
#   track_start_date: new Date(), # absolute datetime of now - position of current time
#   bitrate: 192, # number of kbps
# }
# search_results structure mimics library structure

######################### global #####################
window.WEB_SOCKET_SWF_LOCATION = "/public/vendor/socket.io/WebSocketMain.swf"


######################### static #####################
DEFAULT_ARTIST = "[Unknown Artist]"
DEFAULT_ALBUM = "[Unknown Album]"
VARIOUS_ARTISTS = "Various Artists"

MPD_SENTINEL = /^(OK|ACK|list_OK)(.*)$/m

elapsedToDate = (elapsed) -> new Date((new Date()) - elapsed * 1000)
dateToElapsed = (date) -> ((new Date()) - date) / 1000

fromMpdVol = (vol) -> vol / 100
toMpdVol = (vol) -> Math.round(parseFloat(vol) * 100)

startsWith = (string, str) -> string.substring(0, str.length) == str
stripPrefixes = ['the ', 'a ', 'an ']
sortableTitle = (title) ->
  t = title.toLowerCase()
  for prefix in stripPrefixes
    if startsWith(t, prefix)
      t = t.substring(prefix.length)
      break
  t

titleCompare = (a,b) ->
  _a = sortableTitle(a)
  _b = sortableTitle(b)
  if _a < _b
    -1
  else if _a > _b
    1
  else
    # At this point we compare the original strings. Our cache update code
    # depends on this behavior.
    if a < b
      -1
    else if a > b
      1
    else
      0

noop = ->

escape = (str) ->
  # replace all " with \"
  str.toString().replace /"/g, '\\"'

clearArray = (arr) -> arr.length = 0
clearObject = (obj) -> delete obj[prop] for own prop of obj

pickNRandomProps = (obj, n) ->
  results = []
  count = 0
  for prop of obj
    count += 1
    for i in [0...n]
      if Math.random() < 1 / count
        results[i] = prop
  return results

window.Mpd = class _

  ######################### private #####################


  createEventHandlers: =>
    registrarNames = [
      'onError'
      'onLibraryUpdate'
      'onSearchResults'
      'onPlaylistUpdate'
      'onStatusUpdate'
    ]
    @nameToHandlers = {}
    createEventRegistrar = (name) =>
      handlers = []
      registrar = (handler) -> handlers.push handler
      registrar._name = name
      @nameToHandlers[name] = handlers
      this[name] = registrar
    createEventRegistrar(name) for name in registrarNames

  raiseEvent: (eventName, args...) =>
    # create copy so handlers can remove themselves
    handlersList = $.extend [], @nameToHandlers[eventName]
    handler(args...) for handler in handlersList

  handleMessage: (msg) =>
    handler = @msgHandlerQueue.shift()
    @debugMsgConsole?.log "get-: #{handler.debug_id}: " + JSON.stringify(msg)
    handler.cb(msg) if msg?

  send: (msg) =>
    @debugMsgConsole?.log "send: #{@msgHandlerQueue[@msgHandlerQueue.length - 1]?.debug_id ? -1}: " + JSON.stringify(msg)
    @socket.emit 'ToMpd', msg + "\n"


  deleteTrack: (track) =>
    delete @library.track_table[track.file]

    # remove albums rendered empty from the missing tracks
    album = track.album
    if album.tracks? and Object.keys(album.tracks).length == 0
      delete @library.album_table[album.key]

  handleIdleResults: (msg) =>
    (@updateFuncs[system.substring(9)] ? noop)() for system in $.trim(msg).split("\n") when system.length > 0

  clearPlaylist: =>
    clearArray @playlist.item_list
    clearObject @playlist.item_table

  anticipatePlayId: (track_id) =>
    item = @playlist.item_table[track_id]
    @status.current_item = item
    @status.state = "play"
    @status.time = item.track.time
    @status.track_start_date = new Date()
    @raiseEvent 'onStatusUpdate'

  anticipateSkip: (direction) =>
    next_item = @playlist.item_list[@status.current_item.pos + direction]
    if next_item?
      @anticipatePlayId next_item.id

  parseMpdTracks: (msg) =>
    if msg == ""
      return []

    # build list of tracks from msg
    mpd_tracks = []
    current_track = null
    flush_current_track = ->
      if current_track != null
        mpd_tracks.push(current_track)
      current_track = {}
    for line in msg.split("\n")
      [key, value] = line.split(": ")
      if key == 'file'
        flush_current_track()
      current_track[key] = value
    flush_current_track()
    mpd_tracks

  parseMaybeUndefNumber = (n) ->
    n = parseInt(n)
    n = "" if isNaN(n)
    return n
  getOrCreate = (key, table, initObjFunc) ->
    result = table[key]
    if not result?
      result = initObjFunc()
      # insert into table
      table[key] = result
    return result
  makeComparator = (order_keys) ->
    (a, b) ->
      for order_key in order_keys
        a = a[order_key]
        b = b[order_key]
        if a < b
          return -1
        if a > b
          return 1
      return 0
  trackComparator = makeComparator ["track", "name"]
  albumComparator = makeComparator ["year", "name"]
  artistComparator = (a, b) ->
    a = a["name"]
    b = b["name"]
    if a < b then -1 else if a == b then 0 else 1
  addTracksToLibrary: (mpd_tracks, library=@library) =>
    # construct tracks and determine set of albums
    library.track_table = {}
    album_table = {}
    for mpd_track in mpd_tracks
      artist_name = $.trim(mpd_track.Artist) || DEFAULT_ARTIST
      track =
        file: mpd_track.file
        name: mpd_track.Title || mpd_track.file.substr mpd_track.file.lastIndexOf('/') + 1
        artist_name: artist_name
        artist_disambiguation: ""
        album_artist_name: mpd_track.AlbumArtist or artist_name
        track: parseMaybeUndefNumber(mpd_track.Track)
        time: parseInt(mpd_track.Time)
        year: parseMaybeUndefNumber(mpd_track.Date)
      library.track_table[mpd_track.file] = track
      album_name = $.trim(mpd_track.Album) || DEFAULT_ALBUM
      album_key = [album_name, track.year].join("\n")
      if album_name == DEFAULT_ALBUM
        album_key = "#{track.album_artist_name}\n#{album_key}"
      album_key = album_key.toLowerCase()
      album = getOrCreate album_key, album_table, -> {name: album_name, year: track.year, tracks: []}
      album.tracks.push track
      album.year = album_year if not album.year?
      track.album = album

    # find compilation albums and create artist objects
    artist_table = {}
    for k, album of album_table
      # count up all the artists and album artists mentioned in this album
      album_artists = {}
      album.tracks.sort trackComparator
      for track in album.tracks
        album_artist_name = track.album_artist_name
        album_artists[album_artist_name.toLowerCase()] = 1
        album_artists[track.artist_name.toLowerCase()] = 1
      artist_count = 0
      for k of album_artists
        artist_count += 1
      if artist_count > 1
        # multiple artists. we're sure it's a compilation album.
        album_artist_name = VARIOUS_ARTISTS
      if album_artist_name == VARIOUS_ARTISTS
        # make sure to disambiguate the artist names
        for track in album.tracks
          track.artist_disambiguation = track.artist_name
      artist = getOrCreate album_artist_name.toLowerCase(), artist_table, -> {name: album_artist_name, albums: []}
      artist.albums.push album

    # collect list of artists and sort albums
    library.artists = []
    various_artist = null
    for k, artist of artist_table
      artist.albums.sort albumComparator
      if artist.name == VARIOUS_ARTISTS
        various_artist = artist
      else
        library.artists.push artist

    # sort artists
    library.artists.sort artistComparator
    # various artists goes first
    library.artists.splice 0, 0, various_artist if various_artist?

  rawSendCmd: (cmd, cb=noop) =>
    @msgHandlerQueue.push
      debug_id: @msgCounter++
      cb: cb
    @send cmd

  handleIdleResultsLoop: (msg) =>
    @handleIdleResults(msg)
    # if we have nothing else to do, idle.
    if @msgHandlerQueue.length == 0
      @rawSendCmd "idle", @handleIdleResultsLoop

  clearLibraryObj: (prop_name) =>
    this[prop_name] =
      artists: []
      track_table: {}

  ######################### public #####################
  
  constructor: ->
    @socket = io.connect(undefined, {'force new connection': true})
    @buffer = ""
    @msgHandlerQueue = []
    # assign to console to enable message passing debugging
    @debugMsgConsole = null #console
    @msgCounter = 0

    # whether we've sent the idle command to mpd
    @idling = false

    @socket.on 'FromMpd', (data) =>
      @buffer += data
      
      loop
        m = @buffer.match(MPD_SENTINEL)
        return if not m?

        msg = @buffer.substring(0, m.index)
        [line, code, str] = m
        if code == "ACK"
          @raiseEvent 'onError', str
          # flush the handler
          @handleMessage null
        else if line.indexOf("OK MPD") == 0
          # new connection, ignore
        else
          @handleMessage msg
        @buffer = @buffer.substring(msg.length+line.length+1)
    @socket.on 'connect', =>
      @updateLibrary()
      @updateStatus()
      @updatePlaylist()

    @createEventHandlers()
    @haveFileListCache = false
    
    # maps mpd subsystems to our function to call which will update ourself
    @updateFuncs =
      database: -> # the song database has been modified after update.
        @haveFileListCache = false
        @updateLibrary()
      update: noop # a database update has started or finished. If the database was modified during the update, the database event is also emitted.
      stored_playlist: noop # a stored playlist has been modified, renamed, created or deleted
      playlist: @updatePlaylist # the current playlist has been modified
      player: @updateStatus # the player has been started, stopped or seeked
      mixer: @updateStatus # the volume has been changed
      output: noop # an audio output has been enabled or disabled
      options: @updateStatus # options like repeat, random, crossfade, replay gain
      sticker: noop # the sticker database has been modified.
      subscription: noop # a client has subscribed or unsubscribed to a channel
      message: noop # a message was received on a channel this client is subscribed to; this event is only emitted when the queue is empty


    # cache of library data from mpd. See comment at top of this file
    @clearLibraryObj 'library'
    # mimics library but it's the search results
    @search_results = @library
    # cache of playlist data from mpd.
    @playlist =
      item_list: []
      item_table: {}
    @status =
      current_item: null

  removeEventListeners: (event_name) =>
    handlers = @nameToHandlers[event_name]
    handlers.length = 0
    
  removeListener: (registrar, handler) =>
    handlers = @nameToHandlers[registrar._name]
    for h, i in handlers
      if h is handler
        handlers.splice i, 1
        return

  sendCommand: (command, callback=noop) =>
    @send("noidle") if @idling
    @rawSendCmd command, callback
    @rawSendCmd "idle", @handleIdleResultsLoop
    @idling = true # we're always idling after the first command.

  sendCommands: (command_list, callback=noop) =>
    return if command_list.length == 0
    @sendCommand "command_list_begin\n#{command_list.join("\n")}\ncommand_list_end", callback

  updateLibrary: =>
    @sendCommand 'listallinfo', (msg) =>
      @addTracksToLibrary @parseMpdTracks msg
      @haveFileListCache = true
      # notify listeners
      @raiseEvent 'onLibraryUpdate'

  updatePlaylist: (callback=noop) =>
    @sendCommand "playlistinfo", (msg) =>
      mpd_tracks = @parseMpdTracks msg
      @clearPlaylist()
      for mpd_track in mpd_tracks
        id = parseInt(mpd_track.Id)
        obj =
          id: id
          track: @library.track_table[mpd_track.file]
          pos: @playlist.item_list.length
        @playlist.item_list.push obj
        @playlist.item_table[id] = obj

      # make sure current track data is correct
      if @status.current_item?
        @status.current_item = @playlist.item_table[@status.current_item.id]

      if @status.current_item?
        # looks good, notify listeners
        @raiseEvent 'onPlaylistUpdate'
        callback()
      else
        # we need a status update before raising a playlist update event
        @updateStatus =>
          callback()
          @raiseEvent 'onPlaylistUpdate'

  updateStatus: (callback=noop) =>
    # can't use await/defer yet:
    # https://github.com/jashkenas/coffee-script/pull/1942#issuecomment-3707044
    @sendCommands ["status"], (msg) =>
      # no dict comprehensions :(
      # https://github.com/jashkenas/coffee-script/issues/77
      o = {}
      for [key, val] in (line.split(": ") for line in msg.split("\n"))
        o[key] = val
      $.extend @status,
        volume: parseInt(o.volume) / 100
        repeat: parseInt(o.repeat) != 0
        random: parseInt(o.random) != 0
        single: parseInt(o.single) != 0
        consume: parseInt(o.consume) != 0
        state: o.state
        time: null
        bitrate: null
        track_start_date: null
      
      @status.bitrate = parseInt(o.bitrate) if o.bitrate?

      if o.time? and o.elapsed?
        @status.time = parseInt(o.time.split(":")[1])
        # we still add elapsed for when its paused
        @status.elapsed = parseFloat(o.elapsed)
        # add a field for the start date of the current track
        @status.track_start_date = elapsedToDate(@status.elapsed)

    @sendCommand "currentsong", (msg) =>
      mpd_tracks = @parseMpdTracks msg
      if mpd_tracks.length == 0
        # no current song
        @status.current_item = null
        callback()
        @raiseEvent 'onStatusUpdate'
        return

      # there's either 0 or 1
      for mpd_track in mpd_tracks
        id = parseInt(mpd_track.Id)
        pos = parseInt(mpd_track.Pos)

        @status.current_item = @playlist.item_table[id]

        if @status.current_item? and @status.current_item.pos == pos
          @status.current_item.track = @library.track_table[mpd_track.file]
          # looks good, notify listeners
          @raiseEvent 'onStatusUpdate'
          callback()
        else
          # missing or inconsistent playlist data, need to get playlist update
          @status.current_item =
            id: id
            pos: pos
            track: @library.track_table[mpd_track.file]
          @updatePlaylist =>
            callback()
            @raiseEvent 'onStatusUpdate'

  # puts the search results in search_results
  search: (query) =>
    query = $.trim(query)
    if query.length == 0
      @search_results = @library
      @raiseEvent 'onSearchResults'
      return
    mpd_query = "search"
    words = query.split(/\s+/)
    for word in words
      mpd_query += " any \"#{escape(word)}\""
    @clearLibraryObj 'search_results'
    @sendCommand mpd_query, (msg) =>
      @addTracksToLibrary @parseMpdTracks(msg), @search_results
      @raiseEvent 'onSearchResults'

  queueRandomTracks: (n) =>
    if @haveFileListCache
      @sendCommands ("add \"#{escape(file)}\"" for file in pickNRandomProps(@library.track_table, n))

  queueFile: (file) =>
    @sendCommand "add \"#{escape(file)}\""
    item =
      id: null
      pos: @playlist.item_list.length
      track: @library.track_table[file]
    @playlist.item_list.push item
    @raiseEvent 'onPlaylistUpdate'

  queueFileNext: (file) =>
    cur_pos = @status.current_item?.pos
    if not cur_pos
      @queueFile file
      return
    new_pos = cur_pos + 1
    item =
      id: null
      pos: @playlist.item_list.length
      track: @library.track_table[file]
    @sendCommand "addid \"#{escape(file)}\" #{new_pos}", (msg) =>
      item.id = parseInt(msg.substring(4))
      @playlist.item_table[item.id] = item

    @playlist.item_list.splice new_pos, 0, item
    # fix the pos property of each item
    item.pos = i for item, i in @playlist.item_list
    @raiseEvent 'onPlaylistUpdate'

  clear: =>
    @sendCommand "clear"
    @clearPlaylist()
    @raiseEvent 'onPlaylistUpdate'

  stop: =>
    @sendCommand "stop"
    @status.state = "stop"
    @raiseEvent 'onStatusUpdate'

  play: =>
    @sendCommand "play"

    if @status.state is "pause"
      @status.track_start_date = elapsedToDate(@status.elapsed)
      @status.state = "play"
      @raiseEvent 'onStatusUpdate'

  pause: =>
    @sendCommand "pause 1"

    if @status.state is "play"
      @status.elapsed = dateToElapsed(@status.track_start_date)
      @status.state = "pause"
      @raiseEvent 'onStatusUpdate'

  next: =>
    @sendCommand "next"
    @anticipateSkip 1

  prev: =>
    @sendCommand "previous"
    @anticipateSkip -1

  playId: (track_id) =>
    track_id = parseInt(track_id)
    @sendCommand "playid #{escape(track_id)}"
    @anticipatePlayId track_id

  removeId: (track_id) =>
    track_id = parseInt(track_id)
    if @status.current_item?.id == track_id
      @anticipateSkip 1
      if @status.state isnt "play"
        @status.state = "stop"
    @sendCommand "deleteid #{escape(track_id)}"
    item = @playlist.item_table[track_id]
    delete @playlist.item_table[item.id]
    @playlist.item_list.splice(item.pos, 1)
    it.pos = index for it, index in @playlist.item_list
    @raiseEvent 'onPlaylistUpdate'

  close: => @send "close" # bypass message queue

  # in seconds
  seek: (pos) =>
    pos = parseFloat(pos)
    @sendCommand "seekid #{@status.current_item.id} #{Math.round(pos)}"
    @status.track_start_date = elapsedToDate(pos)
    @raiseEvent 'onStatusUpdate'

  # between 0 and 1
  setVolume: (vol) =>
    vol = toMpdVol(vol)
    @sendCommand "setvol #{vol}"
    @status.volume = fromMpdVol(vol)
    @raiseEvent 'onStatusUpdate'
