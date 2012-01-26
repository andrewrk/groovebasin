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
  track = context.status.current_item.track
  document.title = "#{track.name} - #{track.artist.name} - #{track.album.name} - #{base_title}"
  $("#nowplaying").html Handlebars.templates.playback(context)

render = ->
  renderPlaylist()
  renderLibrary()
  renderNowPlaying()

attachEventHandlers = ->
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

  $nowplaying = $("#nowplaying")
  $nowplaying.on 'click', '.pause a', (event) ->
    mpd.pause()
    return false

  $nowplaying.on 'click', '.stop a', (event) ->
    mpd.stop()
    return false

  $nowplaying.on 'click', '.play a', (event) ->
    mpd.play()
    return false

  $nowplaying.on 'click', '.skip-prev a', (event) ->
    mpd.prev()
    return false

  $nowplaying.on 'click', '.skip-next a', (event) ->
    mpd.next()
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
  attachEventHandlers()
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

