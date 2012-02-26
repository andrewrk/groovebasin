# convenience
schedule = (delay, func) -> window.setInterval(func, delay)
wait = (delay, func) -> setTimeout func, delay

context =
  playing: -> this.status?.state == 'play'

selection =
  type: null # 'library' or 'playlist'
  playlist_ids: {} # key is id, value is some dummy value
  artist_ids: {}
  album_ids: {}
  track_ids: {}
  cursor: null # the last touched id

mpd = null
base_title = document.title
userIsSeeking = false
userIsVolumeSliding = false
MARGIN = 10

renderPlaylist = ->
  context.playlist = mpd.playlist.item_list
  context.server_status = mpd.server_status
  $playlist = $("#playlist")
  $playlist.html Handlebars.templates.playlist(context)
  # set the state of dynamic mode button
  $("#dynamic-mode")
    .prop("checked", if mpd.server_status?.dynamic_mode then true else false)
    .button("refresh")
  # label the random ones
  cur_id = mpd.status?.current_item?.id
  if mpd.server_status?.random_ids?
    found_random = false
    found_current = not cur_id?
    for pl_item in $playlist.find(".pl-item")
      $pl_item = $(pl_item)
      id = $pl_item.data 'id'
      found_random = true if mpd.server_status.random_ids[id]?
      found_current = true if cur_id == id
      if found_random and found_current and cur_id != id
        $pl_item.addClass "random"
  if cur_id?
    $("#playlist-track-#{cur_id}").addClass('ui-state-hover')

  refreshSelection()

  handleResize()

refreshSelection = ->
  return unless mpd?.playlist?.item_table?

  # clear all selection
  $("#playlist-items .pl-item").removeClass('ui-state-active')

  if selection.type is 'playlist'
    # if any selected ids are not in mpd.playlist, unselect them
    badIds = []
    for id of selection.playlist_ids
      badIds.push id unless mpd.playlist.item_table[id]?
    for id in badIds
      delete selection.playlist_ids[id]

    # highlight selected rows
    for id of selection.playlist_ids
      $playlist_track = $("#playlist-track-#{id}")
      $playlist_track.addClass('ui-state-active') unless $playlist_track.hasClass('ui-state-hover')

renderLibrary = ->
  context.artists = mpd.search_results.artists
  context.empty_library_message = if mpd.haveFileListCache then "No Results" else "loading..."
  $("#library").html Handlebars.templates.library(context)
  handleResize()
  # auto expand small datasets
  $artists = $("#library").children("ul").children("li")
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



updateSliderPos = ->
  return if userIsSeeking
  return if not mpd.status?.time? or not mpd.status.current_item?
  if mpd.status.track_start_date? and mpd.status.state == "play"
    diff_sec = (new Date() - mpd.status.track_start_date) / 1000
  else
    diff_sec = mpd.status.elapsed
  $("#track-slider").slider("option", "value", diff_sec / mpd.status.time)
  $("#nowplaying .elapsed").html formatTime(diff_sec)
  $("#nowplaying .left").html formatTime(mpd.status.time)

renderNowPlaying = ->
  # set window title
  track = mpd.status.current_item?.track
  if track?
    track_display = "#{track.name} - #{track.artist_name}"
    if track.album_name != ""
      track_display += " - " + track.album_name
    document.title = "#{track_display} - #{base_title}"
  else
    track_display = ""
    document.title = base_title

  # set song title
  $("#track-display").html(track_display)

  if mpd.status.state?
    # set correct pause/play icon
    toggle_icon =
      play: ['ui-icon-play', 'ui-icon-pause']
      stop: ['ui-icon-pause', 'ui-icon-play']
    toggle_icon.pause = toggle_icon.stop
    [old_class, new_class] = toggle_icon[mpd.status.state]
    $("#nowplaying .toggle span").removeClass(old_class).addClass(new_class)

    # hide seeker bar if stopped
    $("#track-slider").toggle mpd.status.state isnt "stop"

  updateSliderPos()

  # update volume pos
  if mpd.status?.volume? and not userIsVolumeSliding
    $("#vol-slider").slider 'option', 'value', mpd.status.volume

  handleResize()

render = ->
  renderPlaylist()
  renderLibrary()
  renderNowPlaying()

formatTime = (seconds) ->
  seconds = Math.floor seconds
  minutes = Math.floor seconds / 60
  seconds -= minutes * 60
  hours = Math.floor minutes / 60
  minutes -= hours * 60
  zfill = (n) ->
    if n < 10 then "0" + n else "" + n
  if hours != 0
    return "#{hours}:#{zfill minutes}:#{zfill seconds}"
  else
    return "#{minutes}:#{zfill seconds}"

toggleExpansion = ($li) ->
  $div = $li.find("> div")
  $ul = $li.find("> ul")
  if $div.hasClass('artist')
    if not $li.data('cached')
      $li.data 'cached', true
      $ul.html Handlebars.templates.albums
        albums: mpd.search_results.artist_table[$div.find("span").text().toLowerCase()].albums
      $ul.toggle()

  $ul.toggle()

  old_class = 'ui-icon-triangle-1-se'
  new_class = 'ui-icon-triangle-1-e'
  [new_class, old_class] = [old_class, new_class] if $ul.is(":visible")
  $div.find("div").removeClass(old_class).addClass(new_class)
  return false

handleDeletePressed = ->
  if selection.type is 'playlist'
    mpd.removeIds (id for id of selection.playlist_ids)
    refreshSelection()

togglePlayback = ->
  if mpd.status.state == 'play'
    mpd.pause()
  else
    mpd.play()

setUpUi = ->
  $(document).on 'mouseover', '.hoverable', (event) ->
    $(this).addClass "ui-state-hover"
  $(document).on 'mouseout', '.hoverable', (event) ->
    $(this).removeClass "ui-state-hover"

  $pl_window = $("#playlist-window")
  $pl_window.on 'click', 'button.clear', ->
    mpd.clear()
  $pl_window.on 'click', 'button.shuffle', ->
    mpd.shuffle()
  $pl_window.on 'click', '#dynamic-mode', ->
    value = $(this).prop("checked")
    socket.emit 'DynamicMode', JSON.stringify (value)
    return false
  $pl_window.find(".jquery-button").button()

  $playlist = $("#playlist")
  $playlist.on 'dblclick', '.pl-item', (event) ->
    track_id = $(this).data('id')
    mpd.playId track_id

  removeContextMenu = -> $("#menu").remove()
  $playlist.on 'contextmenu', -> false
  $playlist.on 'mousedown', '.pl-item', (event) ->
    event.preventDefault()
    if event.button == 0
      # selecting / unselecting
      removeContextMenu()
      track_id = $(this).data('id')
      skip_drag = false
      if selection.type isnt 'playlist'
        selection.type = 'playlist'
        (selection.playlist_ids = {})[track_id] = true
        selection.cursor = track_id
      else if event.ctrlKey or event.shiftKey
        skip_drag = true
        if event.shiftKey and not event.ctrlKey
          selection.playlist_ids = {}
        if event.shiftKey
          old_pos = if selection.cursor? then mpd.playlist.item_table[selection.cursor].pos else 0
          new_pos = mpd.playlist.item_table[track_id].pos
          for i in [old_pos..new_pos]
            selection.playlist_ids[mpd.playlist.item_list[i].id] = true
        else if event.ctrlKey
          if selection.playlist_ids[track_id]?
            delete selection.playlist_ids[track_id]
          else
            selection.playlist_ids[track_id] = true
          selection.cursor = track_id
      else if not selection.playlist_ids[track_id]?
        (selection.playlist_ids = {})[track_id] = true
        selection.cursor = track_id

      refreshSelection()
      
      # dragging
      if not skip_drag
        started_drag = false
        start_drag_x = event.pageX
        start_drag_y = event.pageY

        getDragPosition = (x, y) ->
          # loop over the playlist items and find where it fits
          best =
            track_id: null
            distance: null
            direction: null
          for item in $playlist.find(".pl-item").get()
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
          $(document)
            .off('mousemove', onDragMove)
            .off('mouseup', onDragEnd)
            .off('keydown', onKeyDown)

          if started_drag
            $playlist.find(".pl-item").removeClass('border-top').removeClass('border-bottom')

        onKeyDown = (event) ->
          if event.keyCode == 27
            abortDrag()

        onDragMove = (event) ->
          if not started_drag
            dist = Math.pow(event.pageX - start_drag_x, 2) + Math.pow(event.pageY - start_drag_y, 2)
            started_drag = true if dist > 64
            return unless started_drag
          result = getDragPosition(event.pageX, event.pageY)
          $playlist.find(".pl-item").removeClass('border-top').removeClass('border-bottom')
          $("#playlist-track-#{result.track_id}").addClass "border-#{result.direction}"

        onDragEnd = (event) ->
          abortDrag()

          if started_drag
            result = getDragPosition(event.pageX, event.pageY)
            delta =
              top: 0
              bottom: 1
            new_pos = mpd.playlist.item_table[result.track_id].pos + delta[result.direction]
            mpd.moveIds (id for id of selection.playlist_ids), new_pos
          else
            # we didn't end up dragging, select the item
            (selection.playlist_ids = {})[track_id] = true
            selection.cursor = track_id
            refreshSelection()

        $(document)
          .on('mousemove', onDragMove)
          .on('mouseup', onDragEnd)
          .on('keydown', onKeyDown)

        onDragMove event

    else if event.button == 2
      # context menu
      removeContextMenu()

      track_id = parseInt($(this).data('id'))

      if selection.type isnt 'playlist' or not selection.playlist_ids[track_id]?
        selection.type = 'playlist'
        (selection.playlist_ids = {})[track_id] = true
        refreshSelection()

      # adds a new context menu to the document
      $(Handlebars.templates.playlist_menu(mpd.playlist.item_table[track_id]))
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
  $playlist.on 'mousedown', -> false

  # delete context menu
  $(document).on 'mousedown', ->
    removeContextMenu()
    selection.type = null
    refreshSelection()
  $(document).on 'keydown', (event) ->
    handlers =
      27: removeContextMenu
      32: togglePlayback
      46: handleDeletePressed
      191: ->
        if event.shiftKey
          $(Handlebars.templates.shortcuts()).appendTo(document.body)
          $("#shortcuts").dialog
            modal: true
            title: "Keyboard Shortcuts"
            minWidth: 600
            height: $(document).height() - 40
            close: -> $("#shortcuts").remove()
        else
          $("#lib-filter").focus().select()

    if (handler = handlers[event.keyCode])?
      handler()
      return false
    return true

  $library = $("#library")
  $library.on 'dblclick', 'div.track', (event) ->
    mpd.queueFile $(this).data('file')

  $library.on 'click', 'div.expandable > div.ui-icon', (event) ->
    toggleExpansion $(this).closest("li")

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
        wait 0, ->
          $(event.target).val("")
          mpd.search ""
      return false
  $lib_filter.on 'keyup', (event) ->
    mpd.search $(event.target).val()

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

  $("#track-slider").slider
    step: 0.0001
    min: 0
    max: 1
    change: (event, ui) ->
      return if not event.originalEvent?
      mpd.seek ui.value * mpd.status.time
    slide: (event, ui) ->
      $("#nowplaying .elapsed").html formatTime(ui.value * mpd.status.time)
    start: (event, ui) -> userIsSeeking = true
    stop: (event, ui) -> userIsSeeking = false
  setVol = (event, ui) ->
    return if not event.originalEvent?
    mpd.setVolume ui.value
  $("#vol-slider").slider
    step: 0.01
    min: 0
    max: 1
    change: setVol
    start: (event, ui) -> userIsVolumeSliding = true
    stop: (event, ui) -> userIsVolumeSliding = false

  # move the slider along the path
  schedule 100, updateSliderPos

  $lib_tabs = $("#lib-tabs")
  $lib_tabs.on 'mouseover', 'li', (event) ->
    $(this).addClass 'ui-state-hover'
  $lib_tabs.on 'mouseout', 'li', (event) ->
    $(this).removeClass 'ui-state-hover'

  tabs = [
    'library-tab'
    'upload-tab'
    'playlist-tab'
  ]

  unselectTabs = ->
    $lib_tabs.find('li').removeClass 'ui-state-active'
    for tab in tabs
      $("##{tab}").hide()

  for tab in tabs
    do (tab) ->
      $lib_tabs.on 'click', "li.#{tab}", (event) ->
        unselectTabs()
        $(this).addClass 'ui-state-active'
        $("##{tab}").show()

  uploader = new qq.FileUploader
    element: document.getElementById("upload-widget")
    action: '/upload'
    encoding: 'multipart'
    onComplete: -> mpd.sendCommand 'update'



initHandlebars = ->
  Handlebars.registerHelper 'time', formatTime

handleResize = ->
  $nowplaying = $("#nowplaying")
  $lib = $("#library-window")
  $pl_window = $("#playlist-window")

  # go really small to make the window as small as possible
  $nowplaying.width MARGIN
  $pl_window.height MARGIN
  $lib.height MARGIN
  $pl_window.css 'position', 'absolute'
  $lib.css 'position', 'absolute'

  # then fit back up to the window
  $nowplaying.width $(document).width() - MARGIN * 2
  second_layer_top = $nowplaying.offset().top + $nowplaying.height() + MARGIN
  $lib.offset
    left: MARGIN
    top: second_layer_top
  $pl_window.offset
    left: $lib.offset().left + $lib.width() + MARGIN
    top: second_layer_top
  $pl_window.width $(window).width() - $pl_window.offset().left - MARGIN
  $lib.height $(window).height() - $lib.offset().top
  $pl_window.height $lib.height() - MARGIN

  # make the inside containers fit
  $lib_header = $lib.find(".window-header")
  $("#library-items").height $lib.height() - $lib_header.position().top - $lib_header.height() - MARGIN
  $pl_header = $pl_window.find("#playlist .header")
  $("#playlist-items").height $pl_window.height() - $pl_header.position().top - $pl_header.height()

socket = null
$(document).ready ->
  socket = io.connect()
  mpd = new window.SocketMpd socket
  mpd.on 'error', (msg) -> alert msg
  mpd.on 'libraryupdate', renderLibrary
  mpd.on 'playlistupdate', renderPlaylist
  mpd.on 'statusupdate', ->
    renderNowPlaying()
    renderPlaylist()
  mpd.on 'serverstatus', renderPlaylist

  setUpUi()
  initHandlebars()


  render()
  handleResize()


  window._debug_mpd = mpd
  window._debug_context = context

$(window).resize handleResize

