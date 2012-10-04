#depend "util"
#depend "socketmpd"
#depend "jquery-1.7.1.min" bare
#depend "jquery-ui-1.8.17.custom.min" bare
#depend "soundmanager2/soundmanager2-nodebug-jsmin" bare
#depend "fileuploader/fileuploader" bare
#depend "socket.io/socket.io.min" bare

selection =
  ids:
    playlist: {} # key is id, value is some dummy value
    artist: {}
    album: {}
    track: {}
  cursor: null # the last touched id
  type: null # 'playlist', 'artist', 'album', or 'track'
  isLibrary: ->
    return false if not this.type?
    return this.type isnt 'playlist'
  isPlaylist: ->
    return false if not this.type?
    return this.type is 'playlist'
  clear: ->
    this.ids.artist = {}
    this.ids.album = {}
    this.ids.track = {}
    this.ids.playlist = {}
  fullClear: ->
    this.clear()
    this.type = null
    this.cursor = null
  selectOnly: (sel_name, key) ->
    this.clear()
    this.type = sel_name
    this.ids[sel_name][key] = true
    this.cursor = key

server_status = null
permissions = {}
socket = null
mpd = null
base_title = document.title
user_is_seeking = false
user_is_volume_sliding = false
started_drag = false
abortDrag = ->
clickTab = null
trying_to_stream = false
actually_streaming = false
streaming_buffering = false
my_user_id = null
my_user_ids = {}
chat_name_input_visible = false
MARGIN = 10

LoadStatus =
  Init: 0
  NoMpd: 1
  NoServer: 2
  GoodToGo: 3
LoadStatusMsg = [
  'Loading...'
  'mpd is not running on the server.'
  'Server is down.'
]
load_status = LoadStatus.Init

# cache jQuery objects
$document = $(document)
$playlist_items = $("#playlist-items")
$dynamic_mode = $("#dynamic-mode")
$pl_btn_repeat = $("#pl-btn-repeat")
$stream_btn = $("#stream-btn")
$lib_tabs = $("#lib-tabs")
$upload_tab = $("#lib-tabs .upload-tab")
$chat_tab = $("#lib-tabs .chat-tab")
$library = $("#library")
$track_slider = $("#track-slider")
$nowplaying = $("#nowplaying")
$nowplaying_elapsed = $nowplaying.find(".elapsed")
$nowplaying_left = $nowplaying.find(".left")
$vol_slider = $("#vol-slider")
$chat_user_list = $("#chat-user-list")
$chat_list = $("#chat-list")
$chat_user_id_span = $("#user-id")
$settings = $("#settings")
$upload_by_url = $("#upload-by-url")
$main_err_msg = $("#main-err-msg")
$main_err_msg_text = $("#main-err-msg-text")

haveUserName = -> server_status?.user_names[my_user_id]?
getUserName = -> userIdToUserName my_user_id
userIdToUserName = (user_id) ->
  return user_id unless server_status?
  user_name = server_status.user_names[user_id]
  return user_name ? user_id

storeMyUserIds = ->
  localStorage?.my_user_ids = JSON.stringify my_user_ids

setUserName = (new_name) ->
  new_name = $.trim new_name
  localStorage?.user_name = new_name
  socket.emit 'SetUserName', new_name

scrollLibraryToSelection = ->
  return unless (helpers = getSelHelpers())?

  delete helpers.playlist
  scrollThingToSelection $library, helpers

scrollPlaylistToSelection = ->
  return unless (helpers = getSelHelpers())?

  delete helpers.track
  delete helpers.artist
  delete helpers.album
  scrollThingToSelection $playlist_items, helpers

scrollThingToSelection = ($scroll_area, helpers) ->
  top_pos = null
  bottom_pos = null
  for sel_name, [ids, table, $getDiv] of helpers
    for id of ids
      item_top = ($div = $getDiv(id)).offset().top
      item_bottom = item_top + $div.height()
      if not top_pos? or item_top < top_pos
        top_pos = item_top
      if not bottom_pos? or item_bottom > bottom_pos
        bottom_pos = item_bottom
  if top_pos?
    scroll_area_top = $scroll_area.offset().top
    selection_top = top_pos - scroll_area_top
    selection_bottom = bottom_pos - scroll_area_top - $scroll_area.height()
    scroll_amt = $scroll_area.scrollTop()
    if selection_top < 0
      $scroll_area.scrollTop scroll_amt + selection_top
    else if selection_bottom > 0
      $scroll_area.scrollTop scroll_amt + selection_bottom

selectionToFiles = (random=false) ->
  # render selection into a single object by file to remove duplicates
  # works for library only
  track_set = {}
  selRenderArtist = (artist) ->
    selRenderAlbum album for album in artist.albums
  selRenderAlbum = (album) ->
    selRenderTrack track for track in album.tracks
  selRenderTrack = (track) ->
    track_set[track.file] = libPosToArr(getTrackSelPos(track))

  selRenderArtist(mpd.search_results.artist_table[key]) for key of selection.ids.artist
  selRenderAlbum(mpd.search_results.album_table[key]) for key of selection.ids.album
  selRenderTrack(mpd.search_results.track_table[file]) for file of selection.ids.track

  if random
    files = (file for file of track_set)
    Util.shuffle files
    return files
  else
    track_arr = ({file: file, pos: pos} for file, pos of track_set)
    track_arr.sort (a, b) -> Util.compareArrays(a.pos, b.pos)
    return (track.file for track in track_arr)

getDragPosition = (x, y) ->
  # loop over the playlist items and find where it fits
  best =
    track_id: null
    distance: null
    direction: null
  for item in $playlist_items.find(".pl-item").get()
    $item = $(item)
    pos = $item.offset()
    height = $item.height()
    track_id = parseInt($item.data('id'))
    # try the top of this element
    distance = Math.abs(pos.top - y)
    if not best.distance? or distance < best.distance
      best.distance = distance
      best.direction = "top"
      best.track_id = track_id
    # try the bottom
    distance = Math.abs(pos.top + height - y)
    if distance < best.distance
      best.distance = distance
      best.direction = "bottom"
      best.track_id = track_id

  return best

renderSettings = ->
  return unless (api_key = server_status?.lastfm_api_key)?
  context =
    lastfm:
      auth_url: "http://www.last.fm/api/auth/?api_key=#{escape(api_key)}&cb=#{location.protocol}//#{location.host}/"
      username: localStorage.lastfm_username
      session_key: localStorage.lastfm_session_key
      scrobbling_on: localStorage.lastfm_scrobbling_on?
    auth:
      password: localStorage.auth_password
      show_edit: not localStorage.auth_password? or settings_ui.auth.show_edit
      permissions: permissions

  $settings.html Handlebars.templates.settings(context)
  $settings.find(".signout").button()
  $settings.find("#toggle-scrobble").button()
  $settings.find(".auth-cancel").button()
  $settings.find(".auth-save").button()
  $settings.find(".auth-edit").button()
  $settings.find(".auth-clear").button()
  $settings.find("#auth-password").val(settings_ui.auth.password)

scrollChatWindowToBottom = ->
  # for some reason, Infinity goes to the top :/
  $chat_list.scrollTop 1000000

renderChat = ->
  chat_status_text = ""
  if (users = server_status?.users)?
    chat_status_text = " (#{users.length})" if users.length > 1
    user_objects = ({
      "class": if user_id is my_user_id then "chat-user-self" else "chat-user"
      user_name: userIdToUserName user_id
    } for user_id in users)
    # write everyone's name in the chat objects (too bad handlebars can't do this in the template)
    for chat_object in server_status.chats
      chat_object["class"] = if my_user_ids[chat_object.user_id]? then "chat-user-self" else "chat-user"
      chat_object.user_name = userIdToUserName chat_object.user_id
    $chat_user_list.html Handlebars.templates.chat_user_list
      users: user_objects
    $chat_list.html Handlebars.templates.chat_list
      chats: server_status.chats
    scrollChatWindowToBottom()
    $chat_user_id_span.text if chat_name_input_visible then "" else getUserName() + ": "
  $chat_tab.find("span").text("Chat#{chat_status_text}")
  resizeChat()

renderStreamButton = ->
  label = if trying_to_stream
    if actually_streaming
      if streaming_buffering
        "Stream: Buffering"
      else
        "Stream: On"
    else
      "Stream: Paused"
  else
    "Stream: Off"

  # disable stream button if we don't have it set up
  $stream_btn
    .button("option", "disabled", not server_status?.stream_httpd_port?)
    .button("option", "label", label)
    .prop("checked", trying_to_stream)
    .button("refresh")

renderPlaylistButtons = ->
  # set the state of dynamic mode button
  $dynamic_mode
    .prop("checked", if server_status?.dynamic_mode then true else false)
    .button("option", "disabled", not server_status?.dynamic_mode_enabled)
    .button("refresh")

  repeat_state = getRepeatStateName()
  $pl_btn_repeat
    .button("option", "label", "Repeat: #{repeat_state}")
    .prop("checked", repeat_state isnt 'Off')
    .button("refresh")

  renderStreamButton()

  # show/hide upload
  $upload_tab.removeClass("ui-state-disabled")
  $upload_tab.addClass("ui-state-disabled") if not server_status?.upload_enabled


renderPlaylist = ->
  context =
    playlist: mpd.playlist.item_list
    server_status: server_status
  scroll_top = $playlist_items.scrollTop()
  $playlist_items.html Handlebars.templates.playlist(context)
  refreshSelection()
  labelPlaylistItems()
  $playlist_items.scrollTop(scroll_top)

labelPlaylistItems = ->
  cur_item = mpd.status.current_item
  # label the old ones
  $playlist_items.find(".pl-item").removeClass('current').removeClass('old')
  if cur_item? and server_status?.dynamic_mode
    for pos in [0...cur_item.pos]
      if (id = mpd.playlist.item_list[pos]?.id)?
        $("#playlist-track-#{id}").addClass('old')
  # label the random ones
  if server_status?.random_ids?
    for item in mpd.playlist.item_list
      if server_status.random_ids[item.id]
        $("#playlist-track-#{item.id}").addClass('random')
  # label the current one
  $("#playlist-track-#{cur_item.id}").addClass('current') if cur_item?

getSelHelpers = ->
  return null unless mpd?.playlist?.item_table?
  return null unless mpd?.search_results?.artist_table?
  return {} =
    playlist: [selection.ids.playlist, mpd.playlist.item_table, (id) -> $("#playlist-track-#{id}")]
    artist: [selection.ids.artist, mpd.search_results.artist_table, (id) -> $("#lib-artist-#{Util.toHtmlId(id)}")]
    album: [selection.ids.album, mpd.search_results.album_table, (id) -> $("#lib-album-#{Util.toHtmlId(id)}")]
    track: [selection.ids.track, mpd.search_results.track_table, (id) -> $("#lib-track-#{Util.toHtmlId(id)}")]

refreshSelection = ->
  return unless (helpers = getSelHelpers())?

  # clear all selection
  $playlist_items.find(".pl-item").removeClass('selected').removeClass('cursor')
  $library.find(".artist").removeClass('selected').removeClass('cursor')
  $library.find(".album").removeClass('selected').removeClass('cursor')
  $library.find(".track").removeClass('selected').removeClass('cursor')

  return unless selection.type?

  for sel_name, [ids, table, $getDiv] of helpers
    # if any selected artists are not in mpd.search_results, unselect them
    delete ids[id] for id in (id for id of ids when not table[id]?)

    # highlight selected rows
    $getDiv(id).addClass 'selected' for id of ids

    if selection.cursor? and sel_name is selection.type
      $getDiv(selection.cursor).addClass('cursor')

renderLibrary = ->
  context =
    artists: mpd.search_results.artists
    empty_library_message: if mpd.have_file_list_cache then "No Results" else "loading..."

  scroll_top = $library.scrollTop()
  $library.html Handlebars.templates.library(context)
  # auto expand small datasets
  $artists = $library.children("ul").children("li")
  node_count = $artists.length
  node_count_limit = 20
  expand_stuff = ($li_set) ->
    for li in $li_set
      $li = $(li)
      return if node_count >= node_count_limit
      $ul = $li.children("ul")
      $sub_li_set = $ul.children("li")
      proposed_node_count = node_count + $sub_li_set.length
      if proposed_node_count <= node_count_limit
        toggleExpansion $li
        # get these vars again because they might have been dynamically added
        # by toggleExpansion
        $ul = $li.children("ul")
        $sub_li_set = $ul.children("li")
        node_count = proposed_node_count
        expand_stuff $sub_li_set
  expand_stuff $artists

  $library.scrollTop(scroll_top)
  refreshSelection()

# returns how many seconds we are into the track
getCurrentTrackPosition = ->
  if mpd.status.track_start_date? and mpd.status.state is "play"
    (new Date() - mpd.status.track_start_date) / 1000
  else
    mpd.status.elapsed

updateSliderPos = ->
  return if user_is_seeking
  if (time = mpd.status.time)? and mpd.status.current_item? and (mpd.status.state ? "stop") isnt "stop"
    disabled = false
    elapsed = getCurrentTrackPosition()
    slider_pos = elapsed / time
  else
    disabled = true
    elapsed = time = slider_pos = 0

  $track_slider
    .slider("option", "disabled", disabled)
    .slider("option", "value", slider_pos)
  $nowplaying_elapsed.html Util.formatTime(elapsed)
  $nowplaying_left.html Util.formatTime(time)

renderNowPlaying = ->
  # set window title
  if (track = mpd.status.current_item?.track)?
    track_display = "#{track.name} - #{track.artist_name}"
    if track.album_name.length
      track_display += " - " + track.album_name
    document.title = "#{track_display} - #{base_title}"
    # Easter time!
    if track.name.indexOf("Groove Basin") is 0
      $("html").addClass('groovebasin')
    else
      $("html").removeClass('groovebasin')
    if track.name.indexOf("Never Gonna Give You Up") is 0 and track.artist_name.indexOf("Rick Astley") is 0
      $("html").addClass('nggyu')
    else
      $("html").removeClass('nggyu')
  else
    track_display = "&nbsp;"
    document.title = base_title

  # set song title
  $("#track-display").html(track_display)

  state = mpd.status.state ? "stop"
  # set correct pause/play icon
  toggle_icon =
    play: ['ui-icon-play', 'ui-icon-pause']
    stop: ['ui-icon-pause', 'ui-icon-play']
    pause: ['ui-icon-pause', 'ui-icon-play']
  [old_class, new_class] = toggle_icon[state]
  $nowplaying.find(".toggle span").removeClass(old_class).addClass(new_class)

  # hide seeker bar if stopped
  $track_slider.slider "option", "disabled", state is "stop"

  updateSliderPos()

  # update volume pos
  unless user_is_volume_sliding
    enabled = (vol = mpd.status.volume)?
    $vol_slider.slider 'option', 'value', vol if enabled
    $vol_slider.slider 'option', 'disabled', not enabled

render = ->
  hide_main_err = load_status is LoadStatus.GoodToGo
  $("#playlist-window").toggle(hide_main_err)
  $("#left-window").toggle(hide_main_err)
  $("#nowplaying").toggle(hide_main_err)
  $main_err_msg.toggle(not hide_main_err)
  unless hide_main_err
    document.title = base_title
    $main_err_msg_text.text(LoadStatusMsg[load_status])
    return

  renderPlaylist()
  renderPlaylistButtons()
  renderLibrary()
  renderNowPlaying()
  renderChat()
  renderSettings()

  handleResize()


toggleExpansion = ($li) ->
  $div = $li.find("> div")
  $ul = $li.find("> ul")
  if $div.hasClass('artist')
    if not $li.data('cached')
      $li.data 'cached', true
      $ul.html Handlebars.templates.albums
        albums: mpd.getArtistAlbums($div.find("span").text())
      $ul.toggle()
      refreshSelection()

  $ul.toggle()

  old_class = 'ui-icon-triangle-1-se'
  new_class = 'ui-icon-triangle-1-e'
  [new_class, old_class] = [old_class, new_class] if $ul.is(":visible")
  $div.find("div").removeClass(old_class).addClass(new_class)
  return false

confirmDelete = (files_list) ->
  list_text = files_list.slice(0, 7).join("\n  ")
  if files_list.length > 7
    list_text += "\n  ..."
  song_text = if files_list.length is 1 then "song" else "songs"
  confirm """
  You are about to delete #{files_list.length} #{song_text} permanently:

    #{list_text}
  """

handleDeletePressed = (shift) ->
  if selection.isLibrary()
    files_list = selectionToFiles()
    if not confirmDelete(files_list) then return
    socket.emit 'DeleteFromLibrary', JSON.stringify(files_list)
  else if selection.isPlaylist()
    if shift
      # delete from library
      files_list = (mpd.playlist.item_table[id].track.file for id of selection.ids.playlist)
      if not confirmDelete(files_list) then return
      socket.emit 'DeleteFromLibrary', JSON.stringify(files_list)
      # fall through and also remove the items from the playlist

    # remove items from playlist and select the item next in the list
    pos = mpd.playlist.item_table[selection.cursor].pos
    mpd.removeIds (id for id of selection.ids.playlist)
    pos = mpd.playlist.item_list.length - 1 if pos >= mpd.playlist.item_list.length
    selection.selectOnly 'playlist', mpd.playlist.item_list[pos].id if pos > -1
    refreshSelection()

toggleStreamStatus = ->
  return unless server_status?.stream_httpd_port?
  trying_to_stream = not trying_to_stream
  renderStreamButton()
  updateStreamingPlayer()
  return false

updateStreamingPlayer = ->
  should_stream = trying_to_stream and mpd.status.state is "play"
  return if actually_streaming is should_stream
  if should_stream
    format = server_status.stream_httpd_format
    port = server_status?.stream_httpd_port
    stream_url = "#{location.protocol}//#{location.hostname}:#{port}/stream.#{format}"
    soundManager.destroySound('stream')
    sound = soundManager.createSound
      id: 'stream'
      url: stream_url
      onbufferchange: ->
        streaming_buffering = sound.isBuffering
        renderStreamButton()

    sound.play()
    streaming_buffering = sound.isBuffering
  else
    soundManager.destroySound('stream')
    streaming_buffering = false
  actually_streaming = should_stream
  renderStreamButton()

togglePlayback = ->
  if mpd.status.state is 'play'
    mpd.pause()
  else
    mpd.play()

setDynamicMode = (value) ->
  args =
    dynamic_mode: value
  socket.emit 'DynamicMode', JSON.stringify(args)

toggleDynamicMode = -> setDynamicMode not server_status.dynamic_mode

getRepeatStateName = ->
  if not mpd.status.repeat
    "Off"
  else if mpd.status.repeat and not mpd.status.single
    "All"
  else
    "One"

nextRepeatState = ->
  if not mpd.status.repeat
    mpd.changeStatus
      repeat: true
      single: true
  else if mpd.status.repeat and not mpd.status.single
    mpd.changeStatus
      repeat: false
      single: false
  else
    mpd.changeStatus
      repeat: true
      single: false

keyboard_handlers = do ->
  upDownHandler = (event) ->
    if event.keyCode is 38 # up
      default_index = mpd.playlist.item_list.length - 1
      dir = -1
    else
      default_index = 0
      dir = 1
    if event.ctrlKey
      if selection.isPlaylist()
        # re-order playlist items
        mpd.shiftIds (id for id of selection.ids.playlist), dir
    else
      # change selection
      if selection.isPlaylist()
        next_pos = mpd.playlist.item_table[selection.cursor].pos + dir
        return if next_pos < 0 or next_pos >= mpd.playlist.item_list.length
        selection.cursor = mpd.playlist.item_list[next_pos].id
        selection.clear() unless event.shiftKey
        selection.ids.playlist[selection.cursor] = true
      else if selection.isLibrary()
        next_pos = getLibSelPos(selection.type, selection.cursor)
        if dir > 0 then nextLibPos(next_pos) else prevLibPos(next_pos)
        return if not next_pos.artist?
        selection.clear() unless event.shiftKey
        if next_pos.track?
          selection.type = 'track'
          selection.cursor = next_pos.track.file
        else if next_pos.album?
          selection.type = 'album'
          selection.cursor = next_pos.album.key
        else
          selection.type = 'artist'
          selection.cursor = mpd.artistKey(next_pos.artist.name)
        selection.ids[selection.type][selection.cursor] = true
      else
        selection.selectOnly 'playlist', mpd.playlist.item_list[default_index].id
      refreshSelection()

    scrollPlaylistToSelection() if selection.isPlaylist()
    scrollLibraryToSelection() if selection.isLibrary()

  leftRightHandler = (event) ->
    dir = if event.keyCode is 37 then -1 else 1
    if selection.isLibrary()
      return unless (helpers = getSelHelpers())
      [ids, table, $getDiv] = helpers[selection.type]
      selected_item = table[selection.cursor]
      is_expanded_funcs =
        artist: isArtistExpanded
        album: isAlbumExpanded
        track: -> true
      is_expanded = is_expanded_funcs[selection.type](selected_item)
      $li = $getDiv(selection.cursor).closest("li")
      cursor_pos = getLibSelPos(selection.type, selection.cursor)
      if dir > 0
        # expand and jump to child
        toggleExpansion $li unless is_expanded
      else
        # collapse; if already collapsed, jump to parent
        toggleExpansion $li if is_expanded
    else
      if event.ctrlKey
        if dir > 0 then mpd.next() else mpd.prev()
      else if event.shiftKey
        mpd.seek getCurrentTrackPosition() + dir * mpd.status.time * 0.10
      else
        mpd.seek getCurrentTrackPosition() + dir * 10

  handlers =
    13: # enter
      ctrl:    no
      alt:     null
      shift:   null
      handler: (event) ->
        if selection.isPlaylist()
          mpd.playId selection.cursor
        else if selection.isLibrary()
          queueLibSelection event
    27: # escape
      ctrl:    no
      alt:     no
      shift:   no
      handler: ->
        # if the user is dragging, abort the drag
        if started_drag
          abortDrag()
          return
        # if there's a menu, only remove that
        if $("#menu").get().length > 0
          removeContextMenu()
          return
        selection.fullClear()
        refreshSelection()
    32: # space
      ctrl:    no
      alt:     no
      shift:   no
      handler: togglePlayback
    37: # left
      ctrl:    null
      alt:     no
      shift:   null
      handler: leftRightHandler
    38: # up
      ctrl:    null
      alt:     no
      shift:   null
      handler: upDownHandler
    39: # right
      ctrl:    null
      alt:     no
      shift:   null
      handler: leftRightHandler
    40: # down
      ctrl:    null
      alt:     no
      shift:   null
      handler: upDownHandler
    46: # delete
      ctrl:    no
      alt:     no
      shift:   null
      handler: (event) -> handleDeletePressed(event.shiftKey)
    67: # 'c'
      ctrl:    no
      alt:     no
      shift:   yes
      handler: -> mpd.clear()
    68: # 'd'
      ctrl:    no
      alt:     no
      shift:   no
      handler: toggleDynamicMode
    72: # 'h'
      ctrl:    no
      alt:     no
      shift:   yes
      handler: -> mpd.shuffle()
    76: # 'l'
      ctrl:    no
      alt:     no
      shift:   no
      handler: -> clickTab 'library'
    82: # 'r'
      ctrl:    no
      alt:     no
      shift:   no
      handler: nextRepeatState
    83: # 's'
      ctrl:    no
      alt:     no
      shift:   no
      handler: toggleStreamStatus
    84: # 't'
      ctrl:    no
      alt:     no
      shift:   no
      handler: ->
        clickTab 'chat'
        $("#chat-input").focus().select()
    85: # 'u'
      ctrl:    no
      alt:     no
      shift:   no
      handler: ->
        clickTab 'upload'
        $upload_by_url.focus().select()
    187: # '=' or '+'
      ctrl:    no
      alt:     no
      shift:   null
      handler: -> mpd.setVolume mpd.status.volume + 0.10
    188: # ',' or '<'
      ctrl:    no
      alt:     no
      shift:   null
      handler: -> mpd.prev()
    189: # '-' or '_'
      ctrl:    no
      alt:     no
      shift:   null
      handler: -> mpd.setVolume mpd.status.volume - 0.10
    190: # '.' or '>'
      ctrl:    no
      alt:     no
      shift:   null
      handler: -> mpd.next()
    191: # '/' or '?'
      ctrl:    no
      alt:     no
      shift:   null
      handler: (event) ->
        if event.shiftKey
          $(Handlebars.templates.shortcuts()).appendTo(document.body)
          $("#shortcuts").dialog
            modal: true
            title: "Keyboard Shortcuts"
            minWidth: 600
            height: $document.height() - 40
            close: -> $("#shortcuts").remove()
        else
          clickTab 'library'
          $("#lib-filter").focus().select()

removeContextMenu = -> $("#menu").remove()

isArtistExpanded = (artist) ->
  $li = $("#lib-artist-#{Util.toHtmlId(mpd.artistKey(artist.name))}").closest("li")
  return false unless $li.data('cached')
  return $li.find("> ul").is(":visible")

isAlbumExpanded = (album) ->
  $li = $("#lib-album-#{Util.toHtmlId(album.key)}").closest("li")
  return $li.find("> ul").is(":visible")

getTrackSelPos = (track) ->
  artist: track.album.artist
  album: track.album
  track: track

getLibSelPos = (type, key) ->
  val =
    artist: null
    album: null
    track: null
  if key?
    switch type
      when 'track'
        val.track = mpd.search_results.track_table[key]
        val.album = val.track.album
        val.artist = val.album.artist
      when 'album'
        val.album = mpd.search_results.album_table[key]
        val.artist = val.album.artist
      when 'artist'
        val.artist = mpd.search_results.artist_table[key]
  else
    val.artist = mpd.search_results.artists[0]
  return val

libPosToArr = (lib_pos) -> [lib_pos.artist?.pos, lib_pos.album?.pos, lib_pos.track?.pos]

# modifies in place
prevLibPos = (lib_pos) ->
  if lib_pos.track?
    lib_pos.track = lib_pos.track.album.tracks[lib_pos.track.pos - 1]
  else if lib_pos.album?
    lib_pos.album = lib_pos.artist.albums[lib_pos.album.pos - 1]
    if lib_pos.album? and isAlbumExpanded(lib_pos.album)
      lib_pos.track = lib_pos.album.tracks[lib_pos.album.tracks.length - 1]
  else if lib_pos.artist?
    lib_pos.artist = mpd.search_results.artists[lib_pos.artist.pos - 1]
    if lib_pos.artist? and isArtistExpanded(lib_pos.artist)
      lib_pos.album = lib_pos.artist.albums[lib_pos.artist.albums.length - 1]
      if lib_pos.album? and isAlbumExpanded(lib_pos.album)
        lib_pos.track = lib_pos.album.tracks[lib_pos.album.tracks.length - 1]

# modifies in place
nextLibPos = (lib_pos) ->
  if lib_pos.track?
    lib_pos.track = lib_pos.track.album.tracks[lib_pos.track.pos + 1]
    if not lib_pos.track?
      lib_pos.album = lib_pos.artist.albums[lib_pos.album.pos + 1]
      if not lib_pos.album?
        lib_pos.artist = mpd.search_results.artists[lib_pos.artist.pos + 1]
  else if lib_pos.album?
    if isAlbumExpanded(lib_pos.album)
      lib_pos.track = lib_pos.album.tracks[0]
    else
      lib_pos.artist = mpd.search_results.artists[lib_pos.artist.pos + 1]
      lib_pos.album = null
  else if lib_pos.artist?
    if isArtistExpanded(lib_pos.artist)
      lib_pos.album = lib_pos.artist.albums[0]
    else
      lib_pos.artist = mpd.search_results.artists[lib_pos.artist.pos + 1]

selectLibraryPosition = (lib_pos) ->
  if lib_pos.track?
    selection.ids.track[lib_pos.track.file] = true
  else if lib_pos.album?
    selection.ids.album[lib_pos.album.key] = true
  else if lib_pos.artist?
    selection.ids.artist[mpd.artistKey(lib_pos.artist.name)] = true

queueFilesPos = ->
  pos = mpd.playlist.item_list.length
  return pos unless server_status?
  for item, i in mpd.playlist.item_list
    return i if server_status.random_ids[item.id]?

queueLibSelection = (event) ->
  files = selectionToFiles(event.altKey)
  if event.shiftKey
    mpd.queueFilesNext files
  else
    mpd.queueFiles files, queueFilesPos()
  return false

settings_ui =
  auth:
    show_edit: false
    password: ""

sendAuth = ->
  pass = localStorage.auth_password
  return unless pass?
  mpd.authenticate pass, (err) ->
    if err
      delete localStorage.auth_password
    renderSettings()
  socket.emit 'Password', pass

settingsAuthSave = ->
  settings_ui.auth.show_edit = false
  $text_box = $("#auth-password")
  localStorage.auth_password = $text_box.val()
  # try to auth
  renderSettings()
  sendAuth()

settingsAuthCancel = ->
  settings_ui.auth.show_edit = false
  renderSettings()

performDrag = (event, callbacks) ->
  abortDrag()
  start_drag_x = event.pageX
  start_drag_y = event.pageY

  abortDrag = ->
    $document
      .off('mousemove', onDragMove)
      .off('mouseup', onDragEnd)

    if started_drag
      $playlist_items.find(".pl-item").removeClass('border-top').removeClass('border-bottom')
      started_drag = false

    abortDrag = ->

  onDragMove = (event) ->
    if not started_drag
      dist = Math.pow(event.pageX - start_drag_x, 2) + Math.pow(event.pageY - start_drag_y, 2)
      started_drag = true if dist > 64
      return unless started_drag
    result = getDragPosition(event.pageX, event.pageY)
    $playlist_items.find(".pl-item").removeClass('border-top').removeClass('border-bottom')
    $("#playlist-track-#{result.track_id}").addClass "border-#{result.direction}"

  onDragEnd = (event) ->
    return false unless event.button is 0

    if started_drag
      callbacks.complete getDragPosition(event.pageX, event.pageY), event
    else
      callbacks.cancel()
    abortDrag()

  $document
    .on('mousemove', onDragMove)
    .on('mouseup', onDragEnd)

  onDragMove event

setUpUi = ->
  $document.on 'mouseover', '.hoverable', (event) ->
    $(this).addClass "ui-state-hover"
  $document.on 'mouseout', '.hoverable', (event) ->
    $(this).removeClass "ui-state-hover"
  $(".jquery-button").button()

  $pl_window = $("#playlist-window")
  $pl_window.on 'click', 'button.clear', ->
    mpd.clear()
  $pl_window.on 'click', 'button.shuffle', ->
    mpd.shuffle()
  $pl_btn_repeat.on 'click', ->
    nextRepeatState()
  $dynamic_mode.on 'click', ->
    value = $(this).prop("checked")
    setDynamicMode(value)
    return false

  $playlist_items.on 'dblclick', '.pl-item', (event) ->
    track_id = $(this).data('id')
    mpd.playId track_id

  $playlist_items.on 'contextmenu', (event) -> return event.altKey
  $playlist_items.on 'mousedown', '.pl-item', (event) ->
    return true if started_drag
    # if any text box has focus, unfocus it
    $(document.activeElement).blur()
    if event.button is 0
      event.preventDefault()
      # selecting / unselecting
      removeContextMenu()
      track_id = $(this).data('id')
      skip_drag = false
      if not selection.isPlaylist()
        selection.selectOnly 'playlist', track_id
      else if event.ctrlKey or event.shiftKey
        skip_drag = true
        if event.shiftKey and not event.ctrlKey
          selection.clear()
        if event.shiftKey
          old_pos = if selection.cursor? then mpd.playlist.item_table[selection.cursor].pos else 0
          new_pos = mpd.playlist.item_table[track_id].pos
          for i in [old_pos..new_pos]
            selection.ids.playlist[mpd.playlist.item_list[i].id] = true
        else if event.ctrlKey
          if selection.ids.playlist[track_id]?
            delete selection.ids.playlist[track_id]
          else
            selection.ids.playlist[track_id] = true
          selection.cursor = track_id
      else if not selection.ids.playlist[track_id]?
        selection.selectOnly 'playlist', track_id

      refreshSelection()
      
      # dragging
      if not skip_drag
        performDrag event,
          complete: (result, event) ->
            delta =
              top: 0
              bottom: 1
            new_pos = mpd.playlist.item_table[result.track_id].pos + delta[result.direction]
            mpd.moveIds (id for id of selection.ids.playlist), new_pos
          cancel: ->
            # we didn't end up dragging, select the item
            selection.selectOnly 'playlist', track_id
            refreshSelection()
    else if event.button is 2
      return if event.altKey
      event.preventDefault()

      # context menu
      removeContextMenu()

      track_id = parseInt($(this).data('id'))

      if not selection.isPlaylist() or not selection.ids.playlist[track_id]?
        selection.selectOnly 'playlist', track_id
        refreshSelection()

      # adds a new context menu to the document
      context =
        item: mpd.playlist.item_table[track_id]
        status: server_status
        permissions: permissions
      $(Handlebars.templates.playlist_menu(context))
        .appendTo(document.body)
      $menu = $("#menu") # get the newly created one
      $menu.offset
        left: event.pageX+1
        top: event.pageY+1
      # don't close menu when you click on the area next to a button
      $menu.on 'mousedown', -> false
      $menu.on 'click', '.remove', ->
        handleDeletePressed(false)
        removeContextMenu()
        return false
      $menu.on 'click', '.download', ->
        removeContextMenu()
        return true
      $menu.on 'click', '.delete', ->
        handleDeletePressed(true)
        removeContextMenu()
        return false

  # don't remove selection in playlist click
  $playlist_items.on 'mousedown', -> false

  # delete context menu
  $document.on 'mousedown', ->
    removeContextMenu()
    selection.type = null
    refreshSelection()
  $document.on 'keydown', (event) ->
    if (handler = keyboard_handlers[event.keyCode])? and
        (not handler.ctrl? or handler.ctrl is event.ctrlKey) and
        (not handler.alt? or handler.alt is event.altKey) and
        (not handler.shift? or handler.shift is event.shiftKey)
      handler.handler event
      return false
    return true

  $library.on 'mousedown', 'div.expandable > div.ui-icon', (event) ->
    toggleExpansion $(this).closest("li")
    return false

  # suppress double click on the icon
  $library.on 'dblclick', 'div.expandable > div.ui-icon', -> false

  $library.on 'dblclick', 'div.artist, div.album, div.track', queueLibSelection

  $library.on 'contextmenu', (event) -> return event.altKey

  libraryMouseDown = (event, sel_name, key) ->
    # if any text box has focus, unfocus it
    $(document.activeElement).blur()
    if event.button is 0
      event.preventDefault()
      removeContextMenu()
      skip_drag = false
      if not selection.isLibrary()
        selection.selectOnly sel_name, key
      else if event.ctrlKey or event.shiftKey
        skip_drag = true
        if event.shiftKey and not event.ctrlKey
          selection.clear()
        if event.shiftKey
          old_pos = getLibSelPos(selection.type, selection.cursor)
          new_pos = getLibSelPos(sel_name, key)

          # swap if positions are out of order
          new_arr = libPosToArr(new_pos)
          old_arr = libPosToArr(old_pos)
          [old_pos, new_pos] = [new_pos, old_pos] if Util.compareArrays(old_arr, new_arr) > 0

          libraryPositionEqual = (old_pos, new_pos) ->
            old_arr = libPosToArr(old_pos)
            new_arr = libPosToArr(new_pos)
            return Util.compareArrays(old_arr, new_arr) is 0

          while old_pos.artist?
            selectLibraryPosition old_pos
            break if libraryPositionEqual(old_pos, new_pos)
            nextLibPos old_pos
        else if event.ctrlKey
          if selection.ids[sel_name][key]?
            delete selection.ids[sel_name][key]
          else
            selection.ids[sel_name][key] = true
          selection.cursor = key
          selection.type = sel_name
      else if not selection.ids[sel_name][key]?
        selection.selectOnly sel_name, key

      refreshSelection()

      # dragging
      if not skip_drag
        performDrag event,
          complete: (result, event) ->
            delta =
              top: 0
              bottom: 1
            new_pos = mpd.playlist.item_table[result.track_id].pos + delta[result.direction]
            files = selectionToFiles(event.altKey)
            mpd.queueFiles files, new_pos
          cancel: ->
            # we didn't end up dragging, select the item
            selection.selectOnly sel_name, key
            refreshSelection()
    else if event.button = 2
      return if event.altKey
      event.preventDefault()

      removeContextMenu()

      if not selection.isLibrary() or not selection.ids[sel_name][key]?
        selection.selectOnly sel_name, key
        refreshSelection()

      # adds a new context menu to the document
      context =
        status: server_status
        permissions: permissions
      if sel_name is 'track'
        context.track = mpd.search_results.track_table[key]
      $(Handlebars.templates.library_menu(context)).appendTo(document.body)
      $menu = $("#menu") # get the newly created one
      $menu.offset
        left: event.pageX+1 # +1 so we don't immediately close the menu by clicking it
        top: event.pageY+1
      # don't close menu when you click on the area next to a button
      $menu.on 'mousedown', -> false
      $menu.on 'click', '.queue', ->
        mpd.queueFiles selectionToFiles()
        removeContextMenu()
        return false
      $menu.on 'click', '.queue-next', ->
        mpd.queueFilesNext selectionToFiles()
        removeContextMenu()
        return false
      $menu.on 'click', '.queue-random', ->
        mpd.queueFiles selectionToFiles(true)
        removeContextMenu()
        return false
      $menu.on 'click', '.queue-next-random', ->
        mpd.queueFilesNext selectionToFiles(true)
        removeContextMenu()
        return false
      $menu.on 'click', '.download', ->
        removeContextMenu()
        return true
      $menu.on 'click', '.delete', ->
        handleDeletePressed(true)
        removeContextMenu()
        return false

  $library.on 'mousedown', '.artist', (event) ->
    artist_key = mpd.artistKey($(this).find("span").text())
    libraryMouseDown event, 'artist', artist_key

  $library.on 'mousedown', '.album', (event) ->
    libraryMouseDown event, 'album', $(this).data('key')

  $library.on 'mousedown', '.track', (event) ->
    libraryMouseDown event, 'track', $(this).data('file')

  $library.on 'mousedown', -> false

  $lib_filter = $("#lib-filter")
  $lib_filter.on 'keydown', (event) ->
    event.stopPropagation()
    switch event.keyCode
      when 27
        # if the box is blank, remove focus
        if $(event.target).val().length is 0
          $(event.target).blur()
        else
          # defer the setting of the text box until after the event loop to
          # work around a firefox bug
          Util.wait 0, ->
            $(event.target).val("")
            mpd.search ""
        return false
      when 13
        # queue all the search results
        files = []
        for artist in mpd.search_results.artists
          for album in artist.albums
            for track in album.tracks
              files.push track.file

        if event.altKey
          Util.shuffle(files)

        if files.length > 2000
          return false unless confirm("You are about to queue #{files.length} songs.")

        if event.shiftKey
          mpd.queueFilesNext files
        else
          mpd.queueFiles files, queueFilesPos()
        return false
      when 40 # down
        # select the first item in the library
        selection.selectOnly 'artist', mpd.artistKey(mpd.search_results.artists[0].name)
        refreshSelection()
        $lib_filter.blur()
        return false
      when 38 # up
        # select the last item in the library
        selection.selectOnly 'artist', mpd.artistKey(mpd.search_results.artists[mpd.search_results.artists.length - 1].name)
        refreshSelection()
        $lib_filter.blur()
        return false
  $lib_filter.on 'keyup', (event) ->
    mpd.search $(event.target).val()

  $chat_name_input = $("#chat-name-input")
  $chat_user_id_span.on 'click', (event) ->
    $chat_input.attr "disabled", "disabled"
    chat_name_input_visible = true
    $chat_name_input.show().val("").focus().select()
    renderChat()
  $chat_name_input.on 'keydown', (event) ->
    event.stopPropagation()
    if event.keyCode is 27
      # cancel
      done = true
    else if event.keyCode is 13
      # accept
      done = true
      setUserName $(event.target).val()
    if done
      chat_name_input_visible = false
      $chat_name_input.hide()
      $chat_input.removeAttr("disabled").focus().select()
      renderChat()
      return false

  $chat_input = $("#chat-input")
  $chat_input.on 'keydown', (event) ->
    event.stopPropagation()
    if event.keyCode is 27
      $(event.target).blur()
      return false
    else if event.keyCode is 13
      message = $.trim($(event.target).val())
      Util.wait 0, ->
        $(event.target).val("")
      return false if message is ""
      unless haveUserName()
        new_user_name = message
      NICK = "/nick "
      if message.substr(0, NICK.length) is NICK
        new_user_name = message.substr(NICK.length)
      if new_user_name?
        setUserName new_user_name
        return false
      socket.emit 'Chat', message
      return false

  actions =
    'toggle': togglePlayback
    'prev': -> mpd.prev()
    'next': -> mpd.next()
    'stop': -> mpd.stop()

  $nowplaying = $("#nowplaying")
  for cls, action of actions
    do (cls, action) ->
      $nowplaying.on 'mousedown', "li.#{cls}", (event) ->
        action()
        return false

  $track_slider.slider
    step: 0.0001
    min: 0
    max: 1
    change: (event, ui) ->
      return if not event.originalEvent?
      mpd.seek ui.value * mpd.status.time
    slide: (event, ui) ->
      $nowplaying_elapsed.html Util.formatTime(ui.value * mpd.status.time)
    start: (event, ui) -> user_is_seeking = true
    stop: (event, ui) -> user_is_seeking = false
  setVol = (event, ui) ->
    return if not event.originalEvent?
    mpd.setVolume ui.value
  $vol_slider.slider
    step: 0.01
    min: 0
    max: 1
    change: setVol
    start: (event, ui) -> user_is_volume_sliding = true
    stop: (event, ui) -> user_is_volume_sliding = false

  # move the slider along the path
  Util.schedule 100, updateSliderPos

  $stream_btn.button
    icons:
      primary: "ui-icon-signal-diag"
  $stream_btn.on 'click', toggleStreamStatus

  $lib_tabs.on 'mouseover', 'li', (event) ->
    $(this).addClass 'ui-state-hover'
  $lib_tabs.on 'mouseout', 'li', (event) ->
    $(this).removeClass 'ui-state-hover'

  tabs = [
    'library'
    'upload'
    'chat'
    'settings'
  ]

  unselectTabs = ->
    $lib_tabs.find('li').removeClass 'ui-state-active'
    for tab in tabs
      $("##{tab}-tab").hide()

  clickTab = (name) ->
    return if name is 'upload' and not server_status?.upload_enabled
    unselectTabs()
    $lib_tabs.find("li.#{name}-tab").addClass 'ui-state-active'
    $("##{name}-tab").show()
    handleResize()
    renderChat() if name is 'chat'

  for tab in tabs
    do (tab) ->
      $lib_tabs.on 'click', "li.#{tab}-tab", (event) ->
        clickTab tab

  uploader = new qq.FileUploader
    element: document.getElementById("upload-widget")
    action: '/upload'
    encoding: 'multipart'

  $settings.on 'click', '.signout', (event) ->
    delete localStorage?.lastfm_username
    delete localStorage?.lastfm_session_key
    delete localStorage?.lastfm_scrobbling_on
    renderSettings()
    return false
  $settings.on 'click', '#toggle-scrobble', (event) ->
    value = $(this).prop("checked")
    if value
      msg = 'LastfmScrobblersAdd'
      localStorage?.lastfm_scrobbling_on = true
    else
      msg = 'LastfmScrobblersRemove'
      delete localStorage?.lastfm_scrobbling_on

    params =
      username: localStorage?.lastfm_username
      session_key: localStorage?.lastfm_session_key
    socket.emit msg, JSON.stringify(params)
    renderSettings()
    return false
  $settings.on 'click', '.auth-edit', (event) ->
    settings_ui.auth.show_edit = true
    renderSettings()
    $text_box = $("#auth-password")
    $text_box.focus().val(localStorage.auth_password ? "").select()
  $settings.on 'click', '.auth-clear', (event) ->
    delete localStorage.auth_password
    settings_ui.auth.password = ""
    renderSettings()
  $settings.on 'click', '.auth-save', (event) ->
    settingsAuthSave()
  $settings.on 'click', '.auth-cancel', (event) ->
    settingsAuthCancel()
  $settings.on 'keydown', '#auth-password', (event) ->
    $text_box = $(this)
    event.stopPropagation()
    settings_ui.auth.password = $text_box.val()
    if event.which is 27
      settingsAuthCancel()
    else if event.which is 13
      settingsAuthSave()
  $settings.on 'keyup', '#auth-password', (event) ->
    settings_ui.auth.password = $(this).val()

  $upload_by_url.on 'keydown', (event) ->
    event.stopPropagation()
    if event.which is 27
      $upload_by_url.val("").blur()
    else if event.which is 13
      url = $upload_by_url.val()
      $upload_by_url.val("").blur()
      socket.emit 'ImportTrackUrl', url

# end setUpUi

initHandlebars = ->
  Handlebars.registerHelper 'time', Util.formatTime
  Handlebars.registerHelper 'artistid', (s) -> "lib-artist-#{Util.toHtmlId(mpd.artistKey(s))}"
  Handlebars.registerHelper 'albumid', (s) -> "lib-album-#{Util.toHtmlId(s)}"
  Handlebars.registerHelper 'trackid', (s) -> "lib-track-#{Util.toHtmlId(s)}"

handleResize = ->
  $nowplaying = $("#nowplaying")
  $left_window = $("#left-window")
  $pl_window = $("#playlist-window")

  # go really small to make the window as small as possible
  $nowplaying.width MARGIN
  $pl_window.height MARGIN
  $left_window.height MARGIN

  # then fit back up to the window
  $nowplaying.width $document.width() - MARGIN * 2
  second_layer_top = $nowplaying.offset().top + $nowplaying.height() + MARGIN
  $left_window.offset
    left: MARGIN
    top: second_layer_top
  $pl_window.offset
    left: $left_window.offset().left + $left_window.width() + MARGIN
    top: second_layer_top
  $pl_window.width $(window).width() - $pl_window.offset().left - MARGIN
  $left_window.height $(window).height() - $left_window.offset().top
  $pl_window.height $left_window.height() - MARGIN

  # make the inside containers fit
  $lib_header = $("#library-tab .window-header")
  $library.height $left_window.height() - $lib_header.position().top - $lib_header.height() - MARGIN
  tab_contents_height = $left_window.height() - $lib_tabs.height() - MARGIN
  $("#upload").height tab_contents_height
  resizeChat()
  $pl_header = $pl_window.find("#playlist .header")
  $playlist_items.height $pl_window.height() - $pl_header.position().top - $pl_header.height()
resizeChat = ->
  height_overshoot = $("#chat-tab").height() - $("#upload").height()
  $chat_list.height $chat_list.height() - height_overshoot

initStreaming = ->
  soundManager.setup
    url: "/vendor/soundmanager2/"
    flashVersion: 9
    debugMode: false

window.WEB_SOCKET_SWF_LOCATION = "/vendor/socket.io/WebSocketMain.swf"
$document.ready ->
  if localStorage?.my_user_ids?
    my_user_ids = JSON.parse localStorage.my_user_ids

  socket = io.connect()

  # special case when we get the callback from Last.fm.
  # tell the server the token and save the session in localStorage.
  # then refresh but remove the "?token=*" from the URL.
  if (token = Util.parseQuery(location.search.substring(1))?.token)?
    socket.emit 'LastfmGetSession', token

    refreshPage = ->
      location.href = "#{location.protocol}//#{location.host}/"
    socket.on 'LastfmGetSessionSuccess', (data) ->
      params = JSON.parse(data)
      localStorage?.lastfm_username = params.session.name
      localStorage?.lastfm_session_key = params.session.key
      delete localStorage?.lastfm_scrobbling_on
      refreshPage()
    socket.on 'LastfmGetSessionError', (data) ->
      params = JSON.parse(data)
      alert "Error authenticating: #{params.message}"
      refreshPage()
    return

  socket.on 'connect', ->
    load_status = LoadStatus.NoMpd
    render()

  socket.on 'Identify', (data) ->
    my_user_id = data.toString()
    my_user_ids[my_user_id] = 1
    storeMyUserIds()
    if (user_name = localStorage?.user_name)?
      setUserName user_name
  socket.on 'Permissions', (data) ->
    permissions = JSON.parse data.toString()
    renderSettings()
  socket.on 'Status', (data) ->
    server_status = JSON.parse data.toString()
    storeMyUserIds()
    renderPlaylistButtons()
    renderChat()
    labelPlaylistItems()
    renderSettings()

    window._debug_server_status = server_status

  mpd = new window.SocketMpd socket
  mpd.on 'libraryupdate', renderLibrary
  mpd.on 'playlistupdate', renderPlaylist
  mpd.on 'statusupdate', ->
    renderNowPlaying()
    renderPlaylistButtons()
    labelPlaylistItems()
    updateStreamingPlayer()
  mpd.on 'chat', renderChat
  mpd.on 'connect', ->
    sendAuth()
    load_status = LoadStatus.GoodToGo
    render()
  socket.on 'disconnect', ->
    load_status = LoadStatus.NoServer
    render()
  socket.on 'MpdDisconnect', ->
    load_status = LoadStatus.NoMpd
    render()

  setUpUi()
  initHandlebars()
  initStreaming()
  render()

  # do this last so that everything becomes the correct size.
  $(window).resize handleResize

  window._debug_mpd = mpd
