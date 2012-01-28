# convenience
schedule = (delay, func) -> window.setInterval(func, delay)

context =
  playing: -> this.status?.state == 'play'

mpd = null
base_title = document.title
userIsSeeking = false

renderPlaylist = ->
  $("#queue").html Handlebars.templates.playlist(context)

renderLibrary = ->
  $("#library").html Handlebars.templates.library(context)

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
    $("#nowplaying .toggle").removeClass(old_class).addClass(new_class)

    # hide seeker bar if stopped
    if context.status.state is "stop"
      $("#track-slider").hide()
    else
      $("#track-slider").show()

  updateSliderPos()

render = ->
  renderPlaylist()
  renderLibrary()
  renderNowPlaying()

formatTime = (seconds) ->
  divide = (numerator, denominator) ->
    [Math.floor(numerator / denominator), numerator % denominator]

  pad = (n, len) ->
    while n.toString().length < len
      n = "0" + n
    n

  [min, sec]  = divide(seconds, 60)
  [hrs, min]  = divide(min, 60)
  sec = pad(Math.floor(sec), 2)
  out = "#{min}:#{sec}"
  out = "#{hrs}:#{out}" if hrs
  out


setUpUi = ->
  $queue = $("#queue")
  $queue.on 'click', 'a.track', (event) ->
    track_id = $(event.target).data('id')
    mpd.playId track_id
    return false
  $queue.on 'click', 'a.remove', (event) ->
    $target = $(event.target)
    track_id = $target.data('id')
    mpd.removeId track_id
    return false
  $queue.on 'click', 'a.clear', ->
    mpd.clear()
    return false
  $queue.on 'click', 'a.randommix', ->
    mpd.queueRandomTracks 1
    return false

  $queue.on 'click', 'a.repopulate', ->
    mpd.clear()
    mpd.queueRandomTracks 20
    return false

  $library = $("#library")
  $library.on 'click', 'a.artist', (event) ->
    artist_name = $(event.target).text()
    mpd.updateArtistInfo(artist_name)
    return false

  $library.on 'click', 'a.track', (event) ->
    file = $(event.target).data('file')
    mpd.queueFile file
    return false

  actions =
    'ui-icon-pause': -> mpd.pause()
    'ui-icon-play': -> mpd.play()
    'ui-icon-seek-prev': -> mpd.prev()
    'ui-icon-seek-next': -> mpd.next()
    'ui-icon-stop': -> mpd.stop()
  $nowplaying = $("#nowplaying")
  for span, action of actions
    do (span, action) ->
      $nowplaying.on 'mousedown', "span.#{span}", (event) ->
        action()
        return false

  seekTrack = (event, ui) ->
    return if not event.originalEvent?
    mpd.seek ui.value * context.status.time
  $("#track-slider").slider
    step: 0.0001
    min: 0
    max: 1
    change: seekTrack
    slide: (event, ui) ->
      $("#nowplaying .elapsed").html formatTime(ui.value * context.status.time)
    start: (event, ui) -> userIsSeeking = true
    stop: (event, ui) -> userIsSeeking = false

  # move the slider along the path
  schedule 100, updateSliderPos

  # debug text box
  $("#line").keydown (event) ->
    if event.keyCode == 13
      line = $("#line").val()
      $("#line").val('')
      mpd.sendCommand line, (msg) ->
        $("#text").val(msg)


initHandlebars = ->
  Handlebars.registerHelper 'hash', (context, options) ->
    ret = ""
    for k,v of context
      ret += options.fn(v)
    ret

$(document).ready ->
  setUpUi()
  initHandlebars()

  mpd = new Mpd()
  mpd.onError (msg) -> alert msg
  mpd.onLibraryUpdate renderLibrary
  mpd.onPlaylistUpdate renderPlaylist
  mpd.onStatusUpdate renderNowPlaying
  context.artists = mpd.library.artist_list
  context.playlist = mpd.playlist.item_list
  context.status = mpd.status

  render()


window._debug_context = context
