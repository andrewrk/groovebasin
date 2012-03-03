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

socket = null
mpd = null
mpd_alive = false
base_title = document.title
user_is_seeking = false
user_is_volume_sliding = false
started_drag = false
abortDrag = null
clickTab = null
stream = null
want_to_queue = []
MARGIN = 10

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
$chat = $("#chat")

flushWantToQueue = ->
  i = 0
  files = []
  while i < want_to_queue.length
    file = want_to_queue[i]
    if mpd.library.track_table[file]?
      files.push file
      want_to_queue.splice i, 1
    else
      i++
  mpd.queueFiles files

scrollPlaylistToSelection = ->
  top_pos = null
  top_id = null
  bottom_pos = null
  bottom_id = null
  for id of selection.ids.playlist
    item_pos = mpd.playlist.item_table[id].pos
    if not top_pos? or item_pos < top_pos
      top_pos = item_pos
      top_id = id
    if not bottom_pos? or item_pos > bottom_pos
      bottom_pos = item_pos
      bottom_id = id
  if top_pos?
    selection_top = $("#playlist-track-#{top_id}").offset().top - $playlist_items.offset().top
    selection_bottom = ($bottom_item = $("#playlist-track-#{bottom_id}")).offset().top + $bottom_item.height() - $playlist_items.offset().top - $playlist_items.height()
    pl_items_scroll = $playlist_items.scrollTop()
    if selection_top < 0
      $playlist_items.scrollTop pl_items_scroll + selection_top
    else if selection_bottom > 0
      $playlist_items.scrollTop pl_items_scroll + selection_bottom

renderChat = ->
  chat_status_text = ""
  if (users = mpd.server_status?.users)?
    # take ourselves out of the list of users
    users = (mpd.userIdToUserName user_id for user_id in users when user_id != mpd.user_id)
    chat_status_text = " (#{users.length})" if users.length > 0
    # write everyone's name in the chat objects (too bad handlebars can't do this in the template)
    for chat_object in mpd.chats
      chat_object.user_name = mpd.userIdToUserName chat_object.user_id
    $chat.html Handlebars.templates.chat
      users: users
      chats: mpd.chats
    if mpd.hasUserName()
      $("#user-id").text(mpd.getUserName() + ": ")
      $("#chat-input").attr('placeholder', "chat")
    else
      $("#user-id").text("")
      $("#chat-input").attr('placeholder', "your name")
  $chat_tab.find("span").text("Chat#{chat_status_text}")

renderPlaylistButtons = ->
  # set the state of dynamic mode button
  $dynamic_mode
    .prop("checked", if mpd.server_status?.dynamic_mode then true else false)
    .button("option", "disabled", not mpd.server_status?.dynamic_mode?)
    .button("refresh")

  repeat_state = getRepeatStateName()
  $pl_btn_repeat
    .button("option", "label", "Repeat: #{repeat_state}")
    .prop("checked", repeat_state isnt 'Off')
    .button("refresh")

  # disable stream button if we don't have it set up
  $stream_btn
    .button("option", "disabled", not mpd.server_status?.stream_httpd_port?)
    .button("refresh")

  # show/hide upload
  $upload_tab.removeClass("ui-state-disabled")
  $upload_tab.addClass("ui-state-disabled") if not mpd.server_status?.upload_enabled

  renderChat()

  labelPlaylistItems()

renderPlaylist = ->
  context =
    playlist: mpd.playlist.item_list
    server_status: mpd.server_status
  scroll_top = $playlist_items.scrollTop()
  $playlist_items.html Handlebars.templates.playlist(context)
  refreshSelection()
  labelPlaylistItems()
  $playlist_items.scrollTop(scroll_top)

labelPlaylistItems = ->
  cur_item = mpd.status?.current_item
  # label the old ones
  $playlist_items.find(".pl-item").removeClass('current').removeClass('old')
  if cur_item? and mpd.server_status?.dynamic_mode
    for pos in [0...cur_item.pos]
      id = mpd.playlist.item_list[pos].id
      $("#playlist-track-#{id}").addClass('old')
  # label the random ones
  if mpd.server_status?.random_ids?
    for item in mpd.playlist.item_list
      if mpd.server_status.random_ids[item.id]
        $("#playlist-track-#{item.id}").addClass('random')
  # label the current one
  $("#playlist-track-#{cur_item.id}").addClass('current') if cur_item?


refreshSelection = ->
  return unless mpd?.playlist?.item_table?
  return unless mpd?.search_results?.artist_table?

  # clear all selection
  $playlist_items.find(".pl-item").removeClass('selected').removeClass('cursor')
  $library.find(".artist").removeClass('selected').removeClass('cursor')
  $library.find(".album").removeClass('selected').removeClass('cursor')
  $library.find(".track").removeClass('selected').removeClass('cursor')

  return unless selection.type?

  things =
    playlist: [selection.ids.playlist, mpd.playlist.item_table, "#playlist-track-", (x) -> x]
    artist: [selection.ids.artist, mpd.search_results.artist_table, "#lib-artist-", Util.toHtmlId]
    album: [selection.ids.album, mpd.search_results.album_table, "#lib-album-", Util.toHtmlId]
    track: [selection.ids.track, mpd.search_results.track_table, "#lib-track-", Util.toHtmlId]
  for sel_name, [ids, table, id_prefix, toId] of things
    # if any selected artists are not in mpd.search_results, unselect them
    delete ids[id] for id in (id for id of ids when not table[id]?)

    # highlight selected rows
    $(id_prefix + toId(id)).addClass 'selected' for id of ids

    if selection.cursor? and sel_name is selection.type
      $(id_prefix + toId(selection.cursor)).addClass('cursor')

renderLibrary = ->
  context =
    artists: mpd.search_results.artists
    empty_library_message: if mpd.haveFileListCache then "No Results" else "loading..."

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
  if mpd.status.track_start_date? and mpd.status.state == "play"
    (new Date() - mpd.status.track_start_date) / 1000
  else
    mpd.status.elapsed

updateSliderPos = ->
  return if user_is_seeking
  if (time = mpd.status?.time)? and mpd.status?.current_item? and (mpd.status?.state ? "stop") isnt "stop"
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
  $track_slider.slider "option", "disabled", state == "stop"

  updateSliderPos()

  # update volume pos
  if (vol = mpd.status?.volume)? and not user_is_volume_sliding
    $vol_slider.slider 'option', 'value', vol

render = ->
  $("#playlist-window").toggle(mpd_alive)
  $("#left-window").toggle(mpd_alive)
  $("#nowplaying").toggle(mpd_alive)
  $("#mpd-error").toggle(not mpd_alive)
  return unless mpd_alive

  renderPlaylist()
  renderPlaylistButtons()
  renderLibrary()
  renderNowPlaying()

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

handleDeletePressed = ->
  if selection.isPlaylist()
    # remove items and select the item next in the list
    pos = mpd.playlist.item_table[selection.cursor].pos
    mpd.removeIds (id for id of selection.ids.playlist)
    pos = mpd.playlist.item_list.length - 1 if pos >= mpd.playlist.item_list.length
    if pos > -1
      selection.cursor = mpd.playlist.item_list[pos].id
      (selection.ids.playlist = {})[selection.cursor] = true if pos > -1
    refreshSelection()

changeStreamStatus = (value) ->
  return unless (port = mpd.server_status?.stream_httpd_port)?
  $stream_btn
    .prop("checked", value)
    .button("refresh")
  if value
    stream = document.createElement("audio")
    stream.setAttribute('src', "#{location.protocol}//#{location.hostname}:#{port}/mpd.ogg")
    stream.setAttribute('autoplay', 'autoplay')
    document.body.appendChild(stream)
    stream.play()
  else
    stream.parentNode.removeChild(stream)
    stream = null

togglePlayback = ->
  if mpd.status.state == 'play'
    mpd.pause()
  else
    mpd.play()

setDynamicMode = (value) ->
  socket.emit 'DynamicMode', JSON.stringify(value)

toggleDynamicMode = -> setDynamicMode not mpd.server_status.dynamic_mode

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
    return unless mpd.playlist.item_list.length

    if event.keyCode == 38 # up
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
        selection.ids.playlist = {} unless event.shiftKey
        selection.ids.playlist[selection.cursor] = true
      else
        selection.selectOnly 'playlist', mpd.playlist.item_list[default_index].id
      refreshSelection()

    scrollPlaylistToSelection() if selection.isPlaylist()

  leftRightHandler = (event) ->
    if event.keyCode == 37 # left
      dir = -1
    else
      dir = 1
    if event.ctrlKey
      if dir > 0
        mpd.next()
      else
        mpd.prev()
    else if event.shiftKey
      mpd.seek getCurrentTrackPosition() + dir * mpd.status.time * 0.10
    else
      mpd.seek getCurrentTrackPosition() + dir * 10

  handlers =
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
      shift:   no
      handler: handleDeletePressed
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
      handler: -> changeStreamStatus not stream?
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
      handler: -> clickTab 'upload'
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
    $("#lib-filter").blur()
    if event.button == 0
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
          selection.ids.playlist = {}
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
        (selection.ids.playlist = {})[track_id] = true
        selection.cursor = track_id

      refreshSelection()
      
      # dragging
      if not skip_drag
        start_drag_x = event.pageX
        start_drag_y = event.pageY

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

        abortDrag = ->
          $document
            .off('mousemove', onDragMove)
            .off('mouseup', onDragEnd)

          if started_drag
            $playlist_items.find(".pl-item").removeClass('border-top').removeClass('border-bottom')
            started_drag = false

        onDragMove = (event) ->
          if not started_drag
            dist = Math.pow(event.pageX - start_drag_x, 2) + Math.pow(event.pageY - start_drag_y, 2)
            started_drag = true if dist > 64
            return unless started_drag
          result = getDragPosition(event.pageX, event.pageY)
          $playlist_items.find(".pl-item").removeClass('border-top').removeClass('border-bottom')
          $("#playlist-track-#{result.track_id}").addClass "border-#{result.direction}"

        onDragEnd = (event) ->
          if started_drag
            result = getDragPosition(event.pageX, event.pageY)
            delta =
              top: 0
              bottom: 1
            new_pos = mpd.playlist.item_table[result.track_id].pos + delta[result.direction]
            mpd.moveIds (id for id of selection.ids.playlist), new_pos

          else
            # we didn't end up dragging, select the item
            selection.selectOnly 'playlist', track_id
            refreshSelection()
          abortDrag()

        $document
          .on('mousemove', onDragMove)
          .on('mouseup', onDragEnd)

        onDragMove event

    else if event.button == 2
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
        status: mpd.server_status
      $(Handlebars.templates.playlist_menu(context))
        .appendTo(document.body)
      $menu = $("#menu") # get the newly created one
      $menu.offset
        left: event.pageX+1
        top: event.pageY+1
      # don't close menu when you click on the area next to a button
      $menu.on 'mousedown', -> false
      $menu.on 'click', '.remove', ->
        handleDeletePressed()
        removeContextMenu()
        return false
      $menu.on 'click', '.download', ->
        removeContextMenu()
        return true

  # don't remove selection in playlist click
  $playlist_items.on 'mousedown', -> false

  # delete context menu
  $document.on 'mousedown', ->
    removeContextMenu()
    selection.type = null
    refreshSelection()
  $document.on 'keydown', (event) ->
    if (handler = keyboard_handlers[event.keyCode])? and
        (not handler.ctrl? or handler.ctrl == event.ctrlKey) and
        (not handler.alt? or handler.alt == event.altKey) and
        (not handler.shift? or handler.shift == event.shiftKey)
      handler.handler event
      return false
    return true

  $library.on 'mousedown', 'div.expandable > div.ui-icon', (event) ->
    toggleExpansion $(this).closest("li")
    return false

  $library.on 'dblclick', 'div.track', (event) ->
    queueFunc = if event.shiftKey then mpd.queueFileNext else mpd.queueFile
    queueFunc $(this).data('file')

  $library.on 'contextmenu', (event) -> return event.altKey

  libraryMouseDown = (event, sel_name, key) ->
    $("#lib-filter").blur()
    if event.button == 0
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

          nextLibraryPosition = (lib_pos) ->
            if lib_pos.track?
              lib_pos.track = lib_pos.track.album.tracks[lib_pos.track.pos + 1]
              if not lib_pos.track?
                lib_pos.album = lib_pos.artist.albums[lib_pos.album.pos + 1]
                if not lib_pos.album?
                  lib_pos.artist = mpd.search_results.artists[lib_pos.artist.pos + 1]
            else if lib_pos.album?
              lib_pos.track = lib_pos.album.tracks[0]
            else if lib_pos.artist?
              lib_pos.album = lib_pos.artist.albums[0]

          selectLibraryPosition = (lib_pos) ->
            if lib_pos.track?
              selection.ids.track[lib_pos.track.file] = true
            else if lib_pos.album?
              selection.ids.album[lib_pos.album.key] = true
            else if lib_pos.artist?
              selection.ids.artist[mpd.artistKey(lib_pos.artist.name)] = true

          while old_pos.artist?
            selectLibraryPosition old_pos
            break if libraryPositionEqual(old_pos, new_pos)
            nextLibraryPosition old_pos
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

      if not skip_drag
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
        status: mpd.server_status
      if sel_name is 'track'
        context.track = mpd.search_results.track_table[key]
      $(Handlebars.templates.library_menu(context)).appendTo(document.body)
      $menu = $("#menu") # get the newly created one
      $menu.offset
        left: event.pageX+1 # +1 so we don't immediately close the menu by clicking it
        top: event.pageY+1
      # don't close menu when you click on the area next to a button
      $menu.on 'mousedown', -> false
      selectionToTrackIds = (random=false) ->
        # render selection into a single object by file to remove duplicates
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
          track_ids = (file for file of track_set)
          Util.shuffle track_ids
          return track_ids
        else
          track_arr = ({file: file, pos: pos} for file, pos of track_set)
          track_arr.sort (a, b) -> Util.compareArrays(a.pos, b.pos)
          return (track.file for track in track_arr)

      $menu.on 'click', '.queue', ->
        mpd.queueFiles selectionToTrackIds()
        removeContextMenu()
        return false
      $menu.on 'click', '.queue-next', ->
        mpd.queueFilesNext selectionToTrackIds()
        removeContextMenu()
        return false
      $menu.on 'click', '.queue-random', ->
        mpd.queueFiles selectionToTrackIds(true)
        removeContextMenu()
        return false
      $menu.on 'click', '.queue-next-random', ->
        mpd.queueFilesNext selectionToTrackIds(true)
        removeContextMenu()
        return false
      $menu.on 'click', '.download', ->
        removeContextMenu()
        return true

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
    if event.keyCode == 27
      # if the box is blank, remove focus
      if $(event.target).val().length == 0
        $(event.target).blur()
      else
        # defer the setting of the text box until after the event loop to
        # work around a firefox bug
        Util.wait 0, ->
          $(event.target).val("")
          mpd.search ""
      return false
    else if event.keyCode == 13
      # queue all the search results
      files = []
      for artist in mpd.search_results.artists
        for album in artist.albums
          for track in album.tracks
            files.push track.file

      if event.ctrlKey
        Util.shuffle(files)

      if files.length > 2000
        return false unless confirm("You are about to queue #{files.length} songs.")

      func = if event.shiftKey then mpd.queueFilesNext else mpd.queueFiles
      func files
      return false
  $lib_filter.on 'keyup', (event) ->
    mpd.search $(event.target).val()

  $("#user-id").on 'click', (event) ->
    localStorage?.user_name = ""
    socket.emit 'SetUserName', ""
    $chat_input.focus().select()

  $chat_input = $("#chat-input")
  $chat_input.on 'keydown', (event) ->
    event.stopPropagation()
    if event.keyCode == 27
      $(event.target).blur()
      return false
    else if event.keyCode == 13
      message = $.trim($(event.target).val())
      Util.wait 0, ->
        $(event.target).val("")
      return false if message == ""
      if not mpd.hasUserName()
        new_user_name = message
      NICK = "/nick "
      if message.substr(0, NICK.length) == NICK
        new_user_name = message.substr(NICK.length)
      if new_user_name?
        localStorage?.user_name = new_user_name
        socket.emit 'SetUserName', new_user_name
        return false
      mpd.sendChat message
      return false

  actions =
    'toggle': togglePlayback
    'prev': mpd.prev
    'next': mpd.next
    'stop': mpd.stop

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
  $stream_btn.on 'click', (event) ->
    value = $(this).prop("checked")
    changeStreamStatus value

  $lib_tabs.on 'mouseover', 'li', (event) ->
    $(this).addClass 'ui-state-hover'
  $lib_tabs.on 'mouseout', 'li', (event) ->
    $(this).removeClass 'ui-state-hover'

  tabs = [
    'library'
    'upload'
    'chat'
  ]

  unselectTabs = ->
    $lib_tabs.find('li').removeClass 'ui-state-active'
    for tab in tabs
      $("##{tab}-tab").hide()

  clickTab = (name) ->
    return if name is 'upload' and not mpd.server_status?.upload_enabled
    unselectTabs()
    $lib_tabs.find("li.#{name}-tab").addClass 'ui-state-active'
    $("##{name}-tab").show()

  for tab in tabs
    do (tab) ->
      $lib_tabs.on 'click', "li.#{tab}-tab", (event) ->
        clickTab tab

  uploader = new qq.FileUploader
    element: document.getElementById("upload-widget")
    action: '/upload'
    encoding: 'multipart'
    onComplete: (id, file_name, response_json) ->
      want_to_queue.push file_name

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
  $("#upload").height $left_window.height() - $lib_tabs.height() - MARGIN
  $pl_header = $pl_window.find("#playlist .header")
  $playlist_items.height $pl_window.height() - $pl_header.position().top - $pl_header.height()

$document.ready ->
  socket = io.connect()
  mpd = new window.SocketMpd socket
  mpd.on 'error', (msg) -> alert msg
  mpd.on 'libraryupdate', ->
    flushWantToQueue()
    renderLibrary()
  mpd.on 'playlistupdate', renderPlaylist
  mpd.on 'statusupdate', ->
    renderNowPlaying()
    renderPlaylistButtons()
  mpd.on 'serverstatus', ->
    renderPlaylistButtons()
  mpd.on 'chat', renderChat
  mpd.on 'connect', ->
    mpd_alive = true
    render()

  setUpUi()
  initHandlebars()

  if (user_name = localStorage?.user_name)?
    socket.emit 'SetUserName', user_name

  $(window).resize handleResize
  render()

  window._debug_mpd = mpd
