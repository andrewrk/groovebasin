# library structure: {
#   artist_list: [sorted list of {artist structure}],
#   artist_table: {"artist name" => {artist structure}}
#   album_list: [sorted list of {album structure}],
#   album_table: {"artist_name-album_name" => {album structure}}
#   track_table: {"track file" => {track structure}}
# }
# album structure:  {
#   name: "Album Name",
#   tracks: {"track file" => {track structure}},
# }
# artist structure: {
#   name: "Artist Name",
#   albums: {"album key" => {album structure}},
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
#   volume: 100, # int 0-100
#   repeat: true, # whether we're in repeat mode. see also `single`
#   random: false, # random mode makes the next song random within the playlist
#   single: true, # true -> repeat the current song, false -> repeat the playlist
#   consume: true, # true -> remove tracks from playlist after done playing
#   state: "play", # or "stop" or "pause"
#   time: 234, # length of song in seconds
#   elapsed: 100.230, # position in current song in seconds
#   bitrate: 192, # number of kbps
# }

window.WEB_SOCKET_SWF_LOCATION = "/public/vendor/socket.io/WebSocketMain.swf"
class Mpd
  ######################### private #####################

  DEFAULT_ARTIST = "[Unknown Artist]"
  DEFAULT_ALBUM = "[Unknown Album]"

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

  bSearch = (list, obj, accessor, compare) ->
    # binary search list
    needle = accessor(obj)
    high = list.length - 1
    low = 0
    while low <= high
      mid = Math.floor((low + high) / 2)
      elem = accessor(list[mid])
      cmp = compare(elem, needle)
      if cmp > 0
        high = mid - 1
      else if cmp < 0
        low = mid + 1
      else
        return [true, mid]

    return [false, low]

  bSearchDelete = (list, obj, accessor, compare) ->
    [found, pos] = bSearch list, obj, accessor, compare
    if not found
      throw "obj not found"
    list.splice pos, 1

  bSearchInsert = (list, obj, accessor, compare) ->
    [found, pos] = bSearch list, obj, accessor, compare
    if found
      throw "obj already exists"
    list.splice pos, 0, obj

  noop = ->

  escape = (str) ->
    # replace all " with \"
    str.toString().replace /"/g, '\\"'

  clearArray = (arr) -> arr.length = 0
  clearObject = (obj) -> delete obj[prop] for own prop of obj

  createEventHandlers: =>
    registrarNames = [
      'onError'
      'onLibraryUpdate'
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
    @debugMsgConsole?.log "get-: #{@msgHandlerQueue[0].debug_id}: " + JSON.stringify(msg)
    @msgHandlerQueue.shift().cb(msg)

  send: (msg) =>
    @debugMsgConsole?.log "send: #{@msgHandlerQueue[@msgHandlerQueue.length - 1]?.debug_id ? -1}: " + JSON.stringify(msg)
    @socket.emit 'ToMpd', msg + "\n"

  getOrCreate: (key, table, list, initObjFunc) =>
    result = table[key]
    if not result?
      result = initObjFunc()
      # insert into table
      table[key] = result
      # insert into sorted list
      bSearchInsert list, result, ((obj) -> obj.name), titleCompare
    return result

  getOrCreateArtist: (artist_name) =>
    @getOrCreate(artist_name, @library.artist_table, @library.artist_list, -> {name: artist_name})

  getOrCreateAlbum: (album_name) =>
    @getOrCreate(album_name, @library.album_table, @library.album_list, -> {name: album_name})

  getOrCreateTrack: (file) =>
    @library.track_table[file] ||= {file: file}

  deleteTrack: (track) =>
    delete @library.track_table[track.file]

    # remove albums rendered empty from the missing tracks
    album = track.album
    if album.tracks? and Object.keys(album.tracks).length == 0
      delete @library.album_table[album_key]
      bSearchDelete @library.album_list, album, ((album) -> album.name), titleCompare

  handleIdleResults: (msg) =>
    (@updateFuncs[system.substring(9)] ? noop)() for system in $.trim(msg).split("\n") when system.length > 0

  ######################### public #####################
  
  MPD_SENTINEL = /^(OK|ACK|list_OK)(.*)$/m
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
        else if line.indexOf("OK MPD") == 0
          # new connection, ignore
        else
          @handleMessage msg
        @buffer = @buffer.substring(msg.length+line.length+1)

    @createEventHandlers()
    
    # maps mpd subsystems to our function to call which will update ourself
    @updateFuncs =
      database: @updateArtistList # the song database has been modified after update.
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
    @library =
      artist_list: []
      artist_table: {}
      album_list: []
      album_table: {}
      track_table: {}
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

  sendCommand: (command, callback=noop) =>
    @send("noidle") if @idling
    @rawSendCmd command, callback
    @rawSendCmd "idle", @handleIdleResultsLoop
    @idling = true # we're always idling after the first command.

  sendCommands: (command_list, callback=noop) =>
    return if command_list.length == 0
    @sendCommand "command_list_begin\n#{command_list.join("\n")}\ncommand_list_end", callback

  updateArtistList: =>
    @sendCommand 'list artist', (msg) =>
      # remove the 'Artist: ' text from every line and convert to array
      newNames = (line.substring(8) for line in msg.split('\n'))
      newNames.sort titleCompare

      # merge with cache
      artistList = @library.artist_list
      artistTable = @library.artist_table
      for newName, i in newNames
        artistEntry = artistList[i]
        oldName = artistEntry?.name
        cmp = titleCompare(oldName, newName) if oldName?
        if i >= artistList.length or cmp > 0 # old > new
          # there's a new artist name. insert it.
          newArtist = {name: newName}
          artistList.splice(i, 0, newArtist)
          artistTable[newName] = newArtist
        else if cmp < 0 # old < new
          # an old artist name no longer exists. remove it.
          # find all the tracks that belong to this artist and remove them
          track_table = @library.track_table
          for album_key, album of artistEntry.albums
            for track_file of album
              @deleteTrack track_table[track_file]

          # remove from artist list and table
          artistList.splice(i, 1)
          delete artistTable[oldName]
          i -= 1

      # delete any remnant old list items
      for oldName in artistList[newNames.length..]
        delete artistTable[oldName]
      artistList[newNames.length..] = []

      # notify listeners
      @raiseEvent 'onLibraryUpdate'

  addTracksToLibrary: (msg, mpdTracksHandler=noop) =>
    if msg == ""
      mpdTracksHandler []
      return

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

    # convert to our track format and add to cache
    track_table = @library.track_table
    for mpd_track in mpd_tracks
      track = @getOrCreateTrack(mpd_track.file)
      $.extend track,
        name: mpd_track.Title
        track: mpd_track.Track
        time: parseInt(mpd_track.Time)
        artist: @getOrCreateArtist(mpd_track.Artist ? DEFAULT_ARTIST)
        album: @getOrCreateAlbum(mpd_track.Album ? DEFAULT_ALBUM)

      album_tracks = track.album.tracks ||= {}
      album_tracks[mpd_track.file] = track

      artist_albums = track.artist.albums ||= {}
      artist_albums[track.album.name] = track.album

    # call the passed in function which might want to do extra things with mpd_tracks
    mpdTracksHandler mpd_tracks

    # notify listeners
    @raiseEvent 'onLibraryUpdate'

  updateArtistInfo: (artist_name) =>
    @sendCommand "find artist \"#{escape(artist_name)}\"", @addTracksToLibrary

  updatePlaylist: (callback=noop) =>
    @sendCommand "playlistinfo", (msg) =>
      @addTracksToLibrary msg, (mpd_tracks) =>
        clearArray @playlist.item_list
        clearObject @playlist.item_table
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
    @sendCommands ["stats", "replay_gain_status", "status"], (msg) =>
      # no dict comprehensions :(
      # https://github.com/jashkenas/coffee-script/issues/77
      o = {}
      for [key, val] in (line.split(": ") for line in msg.split("\n"))
        o[key] = val
      $.extend @status,
        volume: parseInt(o.volume)
        repeat: parseInt(o.repeat) != 0
        random: parseInt(o.random) != 0
        single: parseInt(o.single) != 0
        consume: parseInt(o.consume) != 0
        state: o.state
        time: null
        elapsed: null
        bitrate: null
      
      @status.time = parseInt(o.time.split(":")[1]) if o.time?
      @status.elapsed = parseFloat(o.elapsed) if o.elapsed?
      @status.bitrate = parseInt(o.bitrate) if o.bitrate?

      # temporarily set current item to null until we figure it out in the currentsong command
      @status.current_item = null

    @sendCommand "currentsong", (msg) =>
      @addTracksToLibrary msg, (mpd_tracks) =>
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

  queueFile: (file) =>
    @sendCommand "add \"#{escape(file)}\""
  clear: => @sendCommand "clear"

  stop: => @sendCommand "stop"
  play: => @sendCommand "play"
  pause: => @sendCommand "pause 1"
  next: => @sendCommand "next"
  prev: => @sendCommand "previous"

  playId: (track_id) =>
    @sendCommand "playid #{escape(track_id)}"

  close: => @send "close"
