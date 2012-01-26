context =
  playing: -> this.status?.state == 'play'

mpd = null
base_title = document.title

renderPlaylist = ->
  window._debug_context = context
  $("#queue").html Handlebars.templates.playlist(context)

renderLibrary = ->
  $("#library").html Handlebars.templates.library(context)

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

render = ->
  renderPlaylist()
  renderLibrary()
  renderNowPlaying()

setUpUi = ->
  $("#queue").on 'click', 'a.track', (event) ->
    track_id = $(event.target).data('id')
    mpd.playId track_id
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
    $nowplaying.on 'click', "span.#{span}", (event) ->
      action()
      return false

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
  mpd.updateArtistList()
  mpd.updatePlaylist()

  render()

