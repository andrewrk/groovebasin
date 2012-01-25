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
# playlist structure: [sorted list of {playlist item structure}]
# playlist item structure: {
#   id: 7, # playlist song id
#   track: {track structure},
# }

window.WEB_SOCKET_SWF_LOCATION = "/public/vendor/socket.io/WebSocketMain.swf"
class Mpd
  ######################### private #####################
  
  MPD_INIT = /^OK MPD .+\n$/
  MPD_SENTINEL = /OK\n$/
  MPD_ACK = /^ACK \[\d+@\d+\].*\n$/

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

  createEventHandlers: =>
    registrarNames = [
      'onError'
      'onLibraryUpdate'
      'onPlaylistUpdate'
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
    @msgHandlerQueue.shift()(msg)

  send: (msg) =>
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
    @library.track_table[file] = @library.track_table[file] ? {file: file}

  deleteTrack: (track) =>
    delete @library.track_table[track.file]

    # remove albums rendered empty from the missing tracks
    album = track.album
    if album.tracks? and Object.keys(album.tracks).length == 0
      delete @library.album_table[album_key]
      bSearchDelete @library.album_list, album, ((album) -> album.name), titleCompare

  ######################### public #####################
  
  constructor: ->
    @socket = io.connect()
    @buffer = ""

    @msgHandlerQueue = []

    @socket.on 'FromMpd', (data) =>
      @buffer += data

      if MPD_INIT.test @buffer
        @buffer = ""
      else if MPD_ACK.test @buffer
        @raiseEvent 'onError', @buffer
        @buffer = ""
      else if MPD_SENTINEL.test @buffer
        @handleMessage @buffer.substring(0, @buffer.length-3)
        @buffer = ""

    @createEventHandlers()

    # cache of library data from mpd. See comment at top of this file
    @library =
      artist_list: []
      artist_table: {}
      album_list: []
      album_table: {}
      track_table: {}
    # cache of playlist data from mpd.
    @playlist = []

  removeListener: (registrar, handler) =>
    handlers = @nameToHandlers[registrar._name]
    for h, i in handlers
      if h is handler
        handlers.splice i, 1
        return

  sendCommand: (command, callback=noop) =>
    @msgHandlerQueue.push callback
    @send command

  sendCommands: (command_list, callback=noop) =>
    return if command_list.length == 0
    @msgHandlerQueue.push callback
    @send "command_list_begin\n#{command_list.join("\n")}\ncommand_list_end"

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
    # build list of tracks from msg
    mpd_tracks = {}
    current_file = null
    for line in msg.split("\n")
      [key, value] = line.split(": ")
      if key == 'file'
        current_file = value
        mpd_tracks[current_file] = {}
      mpd_tracks[current_file][key] = value

    # convert to our track format and add to cache
    track_table = @library.track_table
    for file, mpd_track of mpd_tracks
      track = track_table[file] ||= {}
      $.extend track,
        name: mpd_track.Title
        track: mpd_track.Track
        file: file
        time: mpd_track.Time
        artist: @getOrCreateArtist(mpd_track.Artist ? DEFAULT_ARTIST)
        album: @getOrCreateAlbum(mpd_track.Album ? DEFAULT_ALBUM)

      album_tracks = track.album.tracks ||= {}
      album_tracks[file] = track

      artist_albums = track.artist.albums ||= {}
      artist_albums[track.album.name] = track.album

    # call the passed in function which might want to do extra things with mpd_tracks
    mpdTracksHandler mpd_tracks

    # notify listeners
    @raiseEvent 'onLibraryUpdate'

  updateArtistInfo: (artist_name) =>
    @sendCommand "find artist \"#{escape(artist_name)}\"", @addTracksToLibrary

  updatePlaylist: =>
    @sendCommand "playlistinfo", (msg) =>
      @addTracksToLibrary msg, (mpd_tracks) =>
        @playlist.length = 0
        missing_tracks = []
        for file, mpd_track of mpd_tracks
          if not @library.track_table[file]?
            missing_tracks.push file
          @playlist.push
            id: mpd_track.Id
            track: @getOrCreateTrack(file)

        # notify listeners
        @raiseEvent 'onPlaylistUpdate'

        # ask for any missing track details
        commands = ("find file \"#{escape(file)}\"" for file in missing_tracks)
        @sendCommands commands, (msg) =>
          @addTracksToLibrary msg
          @raiseEvent 'onPlaylistUpdate'



  queueFile: (file) =>
    @sendCommand "add \"#{escape(file)}\"", @updatePlaylist

  play: => @sendCommand "play"
  pause: => @sendCommand "pause"
  next: => @sendCommand "next"
  prev: => @sendCommand "previous"

  playId: (track_id) =>
    @sendCommand "playid #{escape(track_id)}"

