# convenience
schedule = (delay, func) -> window.setInterval(func, delay)

context =
  playing: -> this.status?.state == 'play'

mpd = null
base_title = document.title
userIsSeeking = false
userIsVolumeSliding = false
MARGIN = 10

renderPlaylist = ->
  context.playlist = mpd.playlist.item_list
  $playlist = $("#playlist")
  $playlist.html Handlebars.templates.playlist(context)

  if (cur_id = mpd.status?.current_item?.id)?
    $("#playlist-track-#{cur_id}").addClass('ui-state-hover')

  handleResize()

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

setUpUi = ->
  $(document).on 'mouseover', '.hoverable', (event) ->
    $(this).addClass "ui-state-hover"
  $(document).on 'mouseout', '.hoverable', (event) ->
    $(this).removeClass "ui-state-hover"

  $pl_window = $("#playlist-window")
  $pl_window.on 'click', 'a.clear', ->
    mpd.clear()
    return false
  $pl_window.on 'click', 'a.random1', ->
    mpd.queueRandomTracks 1
    return false
  $pl_window.on 'click', 'a.random20', ->
    mpd.queueRandomTracks 20
    return false
  $pl_window.on 'click', 'a.dynamic-mode', ->
    value = $(this).html().indexOf("On") != -1
    value = !value
    socket.emit 'DynamicMode', JSON.stringify (value)
    return false

  $playlist = $("#playlist")
  $playlist.on 'click', 'tr', (event) ->
    track_id = $(this).data('id')
    mpd.playId track_id
    return false
  $playlist.on 'click', 'a.remove', (event) ->
    track_id = $(this).closest("tr").data("id")
    mpd.removeId track_id
    return false

  $library = $("#library")
  $library.on 'click', 'div.track', (event) ->
    mpd.queueFile $(this).data('file')

  $library.on 'click', 'div.expandable', (event) ->
    toggleExpansion $(this).parent()
  wait = (delay, func) -> setTimeout func, delay
  $lib_filter = $("#lib-filter")
  $lib_filter.on 'keydown', (event) ->
    if event.keyCode == 27
      $(event.target).val("")
      mpd.search ""
      return false
  $lib_filter.on 'keyup', (event) ->
      mpd.search $(event.target).val()

  actions =
    'toggle': ->
      if mpd.status.state == 'play'
        mpd.pause()
      else
        mpd.play()
    'prev': -> mpd.prev()
    'next': -> mpd.next()
    'stop': -> mpd.stop()
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
  $lib.height $(window).height() - $lib.offset().top - MARGIN
  $pl_window.height $lib.height()

  # make the inside containers fit
  $lib_header = $lib.find(".window-header")
  $("#library-items").height $lib.height() - $lib_header.position().top - $lib_header.height() - MARGIN
  $pl_header = $pl_window.find(".window-header")
  $("#playlist-items").height $pl_window.height() - $pl_header.position().top - $pl_header.height() - MARGIN

socket = null
$(document).ready ->
  setUpUi()
  initHandlebars()

  socket = io.connect()
  mpd = new window.SocketMpd socket
  mpd.on 'error', (msg) -> alert msg
  mpd.on 'libraryupdate', renderLibrary
  mpd.on 'playlistupdate', renderPlaylist
  mpd.on 'statusupdate', ->
    renderNowPlaying()
    renderPlaylist()
  mpd.on 'serverstatus', ->
    $dynamic_mode_button = $('a.dynamic-mode')
    if mpd.server_status.dynamic_mode
      $dynamic_mode_button.html "Dynamic Mode is On"
    else
      $dynamic_mode_button.html "Dynamic Mode is Off"

  render()
  handleResize()


  window._debug_mpd = mpd
  window._debug_context = context

$(window).resize handleResize

