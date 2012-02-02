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
  $playlist = $("#playlist")
  $playlist.html Handlebars.templates.playlist(context)

  if (cur_id = context.status?.current_item?.id)?
    $("#playlist-track-#{cur_id}").addClass('current')

  handleResize()


renderLibrary = ->
  $("#library").html Handlebars.templates.library(context)
  handleResize()

updateSliderPos = ->
  return if userIsSeeking
  return if not context.status?.time? or not context.status.current_item?
  if context.status.track_start_date? and context.status.state == "play"
    diff_sec = (new Date() - context.status.track_start_date) / 1000
  else
    diff_sec = context.status.elapsed
  $("#track-slider").slider("option", "value", diff_sec / context.status.time)
  $("#nowplaying .elapsed").html formatTime(diff_sec)
  $("#nowplaying .left").html formatTime(context.status.time)

renderNowPlaying = ->
  # set window title
  track = context.status.current_item?.track
  if track?
    track_display = "#{track.name} - #{track.artist.name} - #{track.album.name}"
    document.title = "#{track_display} - #{base_title}"
  else
    track_display = ""
    document.title = base_title

  # set song title
  $("#track-display").html(track_display)

  if context.status.state?
    # set correct pause/play icon
    toggle_icon =
      play: ['ui-icon-play', 'ui-icon-pause']
      stop: ['ui-icon-pause', 'ui-icon-play']
    toggle_icon.pause = toggle_icon.stop
    [old_class, new_class] = toggle_icon[context.status.state]
    $("#nowplaying .toggle span").removeClass(old_class).addClass(new_class)

    # hide seeker bar if stopped
    if context.status.state is "stop"
      $("#track-slider").hide()
    else
      $("#track-slider").show()

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

clearFilter = (event) ->
  if event.keyCode == 27
    $(event.target).val("")
    return false

setUpUi = ->
  $pl_window = $("#playlist-window")
  $pl_window.on 'click', 'a.clear', ->
    mpd.clear()
    return false
  $pl_window.on 'click', 'a.randommix', ->
    mpd.queueRandomTracks 1
    return false
  $pl_window.on 'click', 'a.repopulate', ->
    mpd.queueRandomTracks 20
    return false

  $playlist = $("#playlist")
  $playlist.on 'click', 'a.track', (event) ->
    track_id = $(event.target).data('id')
    mpd.playId track_id
    return false
  $playlist.on 'click', 'a.remove', (event) ->
    $target = $(event.target)
    track_id = $target.data('id')
    mpd.removeId track_id
    return false

  $library = $("#library")
  $library.on 'click', 'div.artist', (event) ->
    $div = $(this)
    artist_name = $div.find("span").text()
    if not $div.data('cached')
      mpd.updateArtistInfo artist_name, ->
        $div.data 'cached', true
        $div.parent().find("> ul").html Handlebars.templates.album_list
          albums: mpd.library.artist_table[artist_name].albums
  $library.on 'click', 'div.track', (event) ->
    mpd.queueFileNext $(this).data('file')

  $library.on 'click', 'div.expandable', (event) ->
    $div = $(this)
    $ul = $div.parent().find("> ul")
    $ul.toggle()

    old_class = 'ui-icon-triangle-1-se'
    new_class = 'ui-icon-triangle-1-e'
    [new_class, old_class] = [old_class, new_class] if $ul.is(":visible")
    $div.find("div").removeClass(old_class).addClass(new_class)
    return false
  $library.on 'mouseover', 'div.hoverable', (event) ->
    $(this).addClass "ui-state-active"
  $library.on 'mouseout', 'div.hoverable', (event) ->
    $(this).removeClass "ui-state-active"

  $library.on 'click', 'li.track', (event) ->
    file = $(event.target).data('file')
    mpd.queueFile file
    return false
  
  $("#lib-filter").on 'keydown', clearFilter
  $("#pl-filter").on 'keydown', clearFilter

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
      mpd.seek ui.value * context.status.time
    slide: (event, ui) ->
      $("#nowplaying .elapsed").html formatTime(ui.value * context.status.time)
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



initHandlebars = ->
  Handlebars.registerHelper 'time', formatTime
  Handlebars.registerHelper 'hash', (context, options) ->
    values = (v for k,v of context)
    if options.hash.orderby?
      order_keys = options.hash.orderby.split(",")
      order_keys.reverse()
      for order_key in order_keys
        values.sort (a, b) ->
          a = a[order_key]
          b = b[order_key]
          if a < b then -1 else if a == b then 0 else 1
    (options.fn(value) for value in values).join("")

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
  $("#library-items").height $lib.height() - $lib.find(".window-header").height() - MARGIN
  $("#playlist-items").height $pl_window.height() - $pl_window.find(".window-header").height() - MARGIN

$(document).ready ->
  setUpUi()
  initHandlebars()

  mpd = new window.Mpd()
  mpd.onError (msg) -> alert msg
  mpd.onLibraryUpdate renderLibrary
  mpd.onPlaylistUpdate renderPlaylist
  mpd.onStatusUpdate ->
    renderNowPlaying()
    renderPlaylist()
  context.artists = mpd.library.artist_list
  context.playlist = mpd.playlist.item_list
  context.status = mpd.status

  render()
  handleResize()

$(window).resize handleResize

window._debug_context = context
