# library structure: {
#   artists: [sorted list of {artist structure}],
#   track_table: {"track file" => {track structure}},
#   artist_table: {"artist name" => {artist structure}},
#   album_table: {"album key" => {album structure}},
# }
# artist structure: {
#   name: "Artist Name",
#   albums: [sorted list of {album structure}],
#   pos: 29, # index into library.artists structure
# }
# album structure:  {
#   name: "Album Name",
#   year: 1999,
#   tracks: [sorted list of {track structure}],
#   artist: {artist structure}
#   pos: 13, # index into artist.albums structure
#   key: "album name", # the index into album_table
# }
# track structure: {
#   name: "Track Name",
#   track: 9,
#   artist_name: "Artist Name",
#   album: {album structure},
#   album_artist_name: "Daft Punk",
#   file: "Obtuse/Cloudy Sky/06. Temple of Trance.mp3",
#   time: 263, # length in seconds
#   pos: 99, # index into album.track structure
# }
# playlist structure: {
#   item_list: [sorted list of {playlist item structure}],
#   item_table: {song id => {playlist item structure}}
# }
# playlist item structure: {
#   id: 7, # playlist song id
#   track: {track structure},
#   pos: 2, # 0-based position in the playlist
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
#   current_item: {playlist item structure},
# }
# search_results structure mimics library structure


# To inherit from this class:
# Define these methods:
#   rawSend: (data) => # send data untouched to mpd
#
# Call these methods:
#   receive: (data) => # when you get data from mpd
#   handleConnectionStart: => # when you first connect

######################### global #####################
_exports = exports ? window

######################### static #####################
DEFAULT_ARTIST = "[Unknown Artist]"
VARIOUS_ARTISTS = "Various Artists"

MPD_SENTINEL = /^(OK|ACK|list_OK)(.*)$/m

__trimLeft = /^\s+/
__trimRight = /\s+$/
__trim = String.prototype.trim
trim = if __trim?
  (text) ->
    if not text? then "" else __trim.call text
else
  (text) ->
    if not text? then "" else text.toString().replace(__trimLeft, "").replace(__trimRight, "")

extend = (obj, args...) ->
  for arg in args
    obj[prop] = val for prop, val of arg
  obj

elapsedToDate = (elapsed) -> new Date((new Date()) - elapsed * 1000)
dateToElapsed = (date) -> ((new Date()) - date) / 1000

bound = (min, val, max) ->
  if val < min
    min
  else if val > max
    max
  else
    val

fromMpdVol = (vol) ->
  vol = parseInt(vol)
  return null if vol < 0 or vol > 100
  return vol / 100
toMpdVol = (vol) -> bound(0, Math.round(parseFloat(vol) * 100), 100)

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

qEscape = (str) ->
  # replace all " with \"
  str.toString().replace /"/g, '\\"'

pickNRandomProps = (obj, n) ->
  return [] if n == 0
  results = []
  count = 0
  for prop of obj
    count += 1
    for i in [0...n]
      if Math.random() < 1 / count
        results[i] = prop
  return results

sign = (n) -> if n > 0 then 1 else if n < 0 then -1 else 0

boolToInt = (b) -> if b then 1 else 0

parseMaybeUndefNumber = (n) ->
  n = parseInt(n)
  n = "" if isNaN(n)
  return n

_exports.split_once = split_once = (line, separator) ->
  # should be line.split(separator, 1), but javascript is stupid
  index = line.indexOf(separator)
  return [line.substr(0, index), line.substr(index + separator.length)]

_exports.trackNameFromFile = trackNameFromFile = (filename) ->
  filetitle = filename.substr filename.lastIndexOf('/') + 1
  len = if (dot = filetitle.lastIndexOf('.')) >= 0 then dot else filetitle.length
  filetitle.substr 0, len

_exports.Mpd = class Mpd

  ######################### private #####################

  on: (event_name, handler) =>
    (@event_handlers[event_name] ?= []).push handler

  raiseEvent: (event_name, args...) =>
    # create copy so handlers can remove themselves
    handlers_list = extend [], @event_handlers[event_name] || []
    handler(args...) for handler in handlers_list

  handleMessage: (msg) =>
    handler = @msgHandlerQueue.shift()
    @debugMsgConsole?.log "get-: #{handler.debug_id}: " + JSON.stringify(msg)
    handler.cb(msg) if msg?
  send: (msg) =>
    @debugMsgConsole?.log "send: #{@msgHandlerQueue[@msgHandlerQueue.length - 1]?.debug_id ? -1}: " + JSON.stringify(msg)
    @rawSend msg

  receive: (data) =>
    @buffer += data

    loop
      m = @buffer.match(MPD_SENTINEL)
      return if not m?

      msg = @buffer.substring(0, m.index)
      [line, code, str] = m
      if code == "ACK"
        @raiseEvent 'error', str
        # flush the handler
        @handleMessage null
      else if line.indexOf("OK MPD") == 0
        # new connection
        @raiseEvent 'connect'
      else
        @handleMessage msg
      @buffer = @buffer.substring(msg.length+line.length+1)

  handleIdleResults: (msg) =>
    (@updateFuncs[system.substring(9)] ? noop)() for system in trim(msg).split("\n") when system.length > 0

  # cache of playlist data from mpd
  clearPlaylist: =>
    @playlist = {}
    @playlist.item_list = []
    @playlist.item_table = {}

  anticipatePlayId: (track_id) =>
    item = @playlist.item_table[track_id]
    @status.current_item = item
    @status.state = "play"
    @status.time = item.track.time
    @status.track_start_date = new Date()
    @raiseEvent 'statusupdate'

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
      [key, value] = split_once line, ": "
      continue if key == 'directory'
      flush_current_track() if key == 'file'
      current_track[key] = value
    flush_current_track()
    mpd_tracks

  mpdTracksToTrackObjects: (mpd_tracks) =>
    tracks = []
    for mpd_track in mpd_tracks
      artist_name = trim(mpd_track.Artist)
      track =
        file: mpd_track.file
        name: mpd_track.Title || trackNameFromFile(mpd_track.file)
        artist_name: artist_name
        artist_disambiguation: ""
        album_artist_name: mpd_track.AlbumArtist or artist_name
        album_name: trim(mpd_track.Album)
        track: parseMaybeUndefNumber(mpd_track.Track)
        time: parseInt(mpd_track.Time)
        year: parseMaybeUndefNumber(mpd_track.Date)
      track.search_tags = [track.artist_name, track.album_artist_name, track.album_name, track.name, track.file].join("\n").toLowerCase()
      tracks.push track
    tracks

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
    titleCompare a.name, b.name
  buildArtistAlbumTree: (tracks, library) =>
    # determine set of albums
    library.track_table = {}
    library.album_table = {}
    for track in tracks
      library.track_table[track.file] = track
      if track.album_name == ""
        album_key = track.album_artist_name + "\n"
      else
        album_key = track.album_name + "\n"
      album_key = album_key.toLowerCase()
      album = getOrCreate album_key, library.album_table, -> {name: track.album_name, year: track.year, tracks: [], key: album_key}
      track.album = album
      album.tracks.push track
      album.year = album_year if not album.year?

    # find compilation albums and create artist objects
    artist_table = {}
    for k, album of library.album_table
      # count up all the artists and album artists mentioned in this album
      album_artists = {}
      album.tracks.sort trackComparator
      for track, i in album.tracks
        # cache the track indexes
        track.pos = i
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
      album.artist = artist
      artist.albums.push album

    # collect list of artists and sort albums
    library.artists = []
    various_artist = null
    for k, artist of artist_table
      artist.albums.sort albumComparator
      # cache the album indexes
      album.pos = i for album, i in artist.albums
      if artist.name == VARIOUS_ARTISTS
        various_artist = artist
      else
        library.artists.push artist

    # sort artists
    library.artists.sort artistComparator
    # various artists goes first
    library.artists.splice 0, 0, various_artist if various_artist?

    # cache the artist indexes
    artist.pos = i for artist, i in library.artists

    library.artist_table = artist_table

  sendWithCallback: (cmd, cb=noop) =>
    @msgHandlerQueue.push
      debug_id: @msgCounter++
      cb: cb
    @send cmd + "\n"

  handleIdleResultsLoop: (msg) =>
    @handleIdleResults(msg)
    # if we have nothing else to do, idle.
    if @msgHandlerQueue.length == 0
      @sendWithCallback "idle", @handleIdleResultsLoop

  fixPlaylistPosCache: => item.pos = i for item, i in @playlist.item_list

  # clear state so we can start over with new mpd connection
  resetServerState: =>
    @buffer = ""
    @msgHandlerQueue = []

    # assign to console to enable message passing debugging
    @debugMsgConsole = null #console
    @msgCounter = 0

    # whether we've sent the idle command to mpd
    @idling = false

    @haveFileListCache = false

    # cache of library data from mpd. See comment at top of this file
    @library =
      artists: []
      track_table: {}
    @search_results = @library
    @last_query = ""
    @clearPlaylist()
    @status =
      current_item: null


  ######################### public #####################

  constructor: ->
    @resetServerState()

    @event_handlers = {}
    
    # maps mpd subsystems to our function to call which will update ourself
    @updateFuncs =
      database: => # the song database has been modified after update.
        @haveFileListCache = false
        @updateLibrary()
      update: noop # a database update has started or finished. If the database was modified during the update, the database event is also emitted.
      stored_playlist: noop # a stored playlist has been modified, renamed, created or deleted
      playlist: @updatePlaylist # the current playlist has been modified
      player: @updateStatus # the player has been started, stopped or seeked
      mixer: @updateStatus # the volume has been changed
      output: noop # an audio output has been enabled or disabled
      options: @updateStatus # options like repeat, random, crossfade, replay gain
      sticker: => # the sticker database has been modified.
        @raiseEvent 'stickerupdate'
      subscription: noop # a client has subscribed or unsubscribed to a channel
      message: noop # a message was received on a channel this client is subscribed to; this event is only emitted when the queue is empty

  removeEventListeners: (event_name) =>
    (@event_handlers[event_name] || []).length = 0
    
  removeListener: (event_name, handler) =>
    handlers = @event_handlers[event_name] || []
    for h, i in handlers
      if h is handler
        handlers.splice i, 1
        return

  artistKey: (artist_name) =>
    if artist_name is DEFAULT_ARTIST then "" else artist_name.toLowerCase()

  getArtistAlbums: (artist_name) =>
    return @search_results.artist_table[@artistKey(artist_name)].albums

  handleConnectionStart: =>
    @sendCommand "subscribe Status"
    @updateLibrary()
    @updateStatus()
    @updatePlaylist()

  sendCommand: (command, callback=noop) =>
    @send "noidle\n" if @idling
    @sendWithCallback command, callback
    @sendWithCallback "idle", @handleIdleResultsLoop
    @idling = true # we're always idling after the first command.

  sendCommands: (command_list, callback=noop) =>
    return if command_list.length == 0
    @sendCommand "command_list_begin\n#{command_list.join("\n")}\ncommand_list_end", callback

  updateLibrary: =>
    @sendCommand 'listallinfo', (msg) =>
      tracks = @mpdTracksToTrackObjects @parseMpdTracks msg
      @buildArtistAlbumTree tracks, @library
      @haveFileListCache = true
      # in case the user has a search open, we'll apply their search again.
      last_query = @last_query
      # reset last query so that search is forced to run again
      @last_query = ""
      @search last_query

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
        @raiseEvent 'playlistupdate'
        callback()
      else
        # we need a status update before raising a playlist update event
        @updateStatus =>
          callback()
          @raiseEvent 'playlistupdate'

  updateStatus: (callback=noop) =>
    @sendCommand "status", (msg) =>
      # no dict comprehensions :(
      # https://github.com/jashkenas/coffee-script/issues/77
      o = {}
      for [key, val] in (split_once(line, ": ") for line in msg.split("\n"))
        o[key] = val
      extend @status,
        volume: fromMpdVol(o.volume)
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
        @raiseEvent 'statusupdate'
        return

      # there's either 0 or 1
      for mpd_track in mpd_tracks
        id = parseInt(mpd_track.Id)
        pos = parseInt(mpd_track.Pos)

        @status.current_item = @playlist.item_table[id]

        if @status.current_item? and @status.current_item.pos == pos
          @status.current_item.track = @library.track_table[mpd_track.file]
          # looks good, notify listeners
          @raiseEvent 'statusupdate'
          callback()
        else
          # missing or inconsistent playlist data, need to get playlist update
          @status.current_item =
            id: id
            pos: pos
            track: @library.track_table[mpd_track.file]
          @updatePlaylist =>
            callback()
            @raiseEvent 'statusupdate'

  # puts the search results in search_results
  search: (query) =>
    query = trim(query)
    if query.length == 0
      @search_results = @library
      @raiseEvent 'libraryupdate'
      @last_query = query
      return
    words = query.toLowerCase().split(/\s+/)
    query = words.join(" ")
    return if query == @last_query
    @last_query = query
    result = []
    for k, track of @library.track_table
      is_match = (->
        for word in words
          if track.search_tags.indexOf(word) == -1
            return false
        return true
      )()
      result.push track if is_match
    # zip results into album
    @buildArtistAlbumTree result, @search_results = {}
    @raiseEvent 'libraryupdate'

  queueRandomTracksCommands: (n) =>
    if not @haveFileListCache
      return []
    ("addid \"#{qEscape(file)}\"" for file in pickNRandomProps(@library.track_table, n))

  queueRandomTracks: (n) =>
    @sendCommands @queueRandomTracksCommands n

  queueFiles: (files, pos=@playlist.item_list.length) =>
    return unless files.length
    cmds = []
    for i in [files.length-1..0]
      file = files[i]
      cmds.push "addid \"#{qEscape(file)}\" #{pos}"

    items = ({id: null, pos: null, track: @library.track_table[file]} for file in files)
    @playlist.item_list.splice pos, 0, items...
    @fixPlaylistPosCache()

    @sendCommands cmds, (msg) =>
      for line, i in msg.split("\n")
        index = files.length - 1 - i
        item_id = parseInt(line.substring(4))
        items[index] = item_id

    @raiseEvent 'playlistupdate'

  queueFile: (file, pos) => @queueFiles [file], pos

  queueFilesNext: (files) =>
    new_pos = (@status.current_item?.pos ? -1) + 1
    @queueFiles files, new_pos

  queueFileNext: (file) => @queueFilesNext [file]

  clear: =>
    @sendCommand "clear"
    @clearPlaylist()
    @raiseEvent 'playlistupdate'

  shuffle: => @sendCommand "shuffle"

  stop: =>
    @sendCommand "stop"
    @status.state = "stop"
    @raiseEvent 'statusupdate'

  play: =>
    @sendCommand "play"

    if @status.state is "pause"
      @status.track_start_date = elapsedToDate(@status.elapsed)
      @status.state = "play"
      @raiseEvent 'statusupdate'

  pause: =>
    @sendCommand "pause 1"

    if @status.state is "play"
      @status.elapsed = dateToElapsed(@status.track_start_date)
      @status.state = "pause"
      @raiseEvent 'statusupdate'

  next: =>
    @sendCommand "next"
    @anticipateSkip 1

  prev: =>
    @sendCommand "previous"
    @anticipateSkip -1

  playId: (track_id) =>
    track_id = parseInt(track_id)
    @sendCommand "playid #{qEscape(track_id)}"
    @anticipatePlayId track_id

  moveIds: (track_ids, pos) =>
    pos = parseInt(pos)
    # get the playlist items for the ids
    items = (item for id in track_ids when (item = @playlist.item_table[id])?)
    # sort the list by the reverse order in the playlist
    items.sort (a, b) -> b.pos - a.pos

    cmds = []
    while items.length > 0
      if pos <= items[0].pos
        real_pos = pos
        item = items.shift()
      else
        real_pos = pos - 1
        item = items.pop()

      cmds.push "moveid #{item.id} #{real_pos}"
      @playlist.item_list.splice item.pos, 1
      @playlist.item_list.splice real_pos, 0, item

      @fixPlaylistPosCache()

    @sendCommands cmds
    @raiseEvent 'playlistupdate'

  # shifts the list of ids by offset, winamp style
  shiftIds: (track_ids, offset) =>
    offset = parseInt(offset)
    return if offset == 0 or track_ids.length == 0

    items = (item for id in track_ids when (item = @playlist.item_table[id])?)
    items.sort (a,b) ->
      sign(offset) * (b.pos - a.pos)

    # abort if any are out of bounds
    for item in [items[0], items[items.length-1]]
      new_pos = item.pos + offset
      return if new_pos < 0 or new_pos >= @playlist.item_list.length

    @sendCommands ("moveid #{item.id} #{item.pos + offset}" for item in items)

    # anticipate the result
    for item in items
      @playlist.item_list.splice item.pos, 1
      @playlist.item_list.splice item.pos+offset, 0, item
      @fixPlaylistPosCache()

    @raiseEvent 'playlistupdate'

  removeIds: (track_ids) =>
    cmds = []
    for track_id in track_ids
      track_id = parseInt(track_id)
      if @status.current_item?.id == track_id
        @anticipateSkip 1
        if @status.state isnt "play"
          @status.state = "stop"
      cmds.push "deleteid #{qEscape(track_id)}"
      item = @playlist.item_table[track_id]
      delete @playlist.item_table[item.id]
      @playlist.item_list.splice(item.pos, 1)
      @fixPlaylistPosCache()

    @sendCommands cmds
    @raiseEvent 'playlistupdate'

  removeId: (track_id) =>
    @removeIds [track_id]

  close: => @send "close\n" # bypass message queue

  # in seconds
  seek: (pos) =>
    pos = parseFloat(pos)
    pos = 0 if pos < 0
    pos = @status.time if pos > @status.time
    @sendCommand "seekid #{@status.current_item.id} #{Math.round(pos)}"
    @status.track_start_date = elapsedToDate(pos)
    @raiseEvent 'statusupdate'

  # between 0 and 1
  setVolume: (vol) =>
    vol = toMpdVol(vol)
    @sendCommand "setvol #{vol}"
    @status.volume = fromMpdVol(vol)
    @raiseEvent 'statusupdate'

  changeStatus: (status) =>
    cmds = []
    if status.consume?
      @status.consume = status.consume
      cmds.push "consume #{boolToInt(status.consume)}"
    if status.random?
      @status.random = status.random
      cmds.push "random #{boolToInt(status.random)}"
    if status.repeat?
      @status.repeat = status.repeat
      cmds.push "repeat #{boolToInt(status.repeat)}"
    if status.single?
      @status.single = status.single
      cmds.push "single #{boolToInt(status.single)}"

    @sendCommands cmds
    @raiseEvent 'statusupdate'

  getFileInfo: (file, cb=noop) =>
    @sendCommand "lsinfo \"#{qEscape(file)}\"", (msg) =>
      [track] = @mpdTracksToTrackObjects @parseMpdTracks msg
      cb track

  authenticate: (password) =>
    @sendCommand "password \"#{qEscape(password)}\""

  scanFiles: (files) =>
    @sendCommands ("update #{file}" for file in files)
