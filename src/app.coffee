context = {}
mpd = null

renderPlaylist = ->
  window.__debug__context = context
  $queue = $("#queue")
  $queue.html Handlebars.templates.playlist(context)

  $queue.find('a.track').click (event) ->
    track_id = $(event.target).data('id')
    mpd.playId track_id
    return false

renderLibrary = ->
  $library = $("#library")
  $library.html Handlebars.templates.library(context)

  $library.find('a.artist').click (event) ->
    artist_name = $(event.target).text()
    mpd.updateArtistInfo(artist_name)
    return false

  $library.find('a.track').click (event) ->
    file = $(event.target).data('file')
    mpd.queueFile file
    return false

renderNowPlaying = ->
  $nowplaying = $("#nowplaying")
  $nowplaying.html Handlebars.templates.playback(context)

  $nowplaying.find('.pause a').click (event) ->
    mpd.pause()
    return false

  $nowplaying.find('.skip-prev a').click (event) ->
    mpd.prev()
    return false

  $nowplaying.find('.skip-next a').click (event) ->
    mpd.next()
    return false

render = ->
  renderPlaylist()
  renderLibrary()
  renderNowPlaying()

$(document).ready ->
  Handlebars.registerHelper 'hash', (context, options) ->
    ret = ""
    for k,v of context
      ret += options.fn(v)
    ret

  mpd = new Mpd()
  mpd.onError (msg) -> alert msg
  mpd.onLibraryUpdate renderLibrary
  mpd.onPlaylistUpdate renderPlaylist
  context.artists = mpd.library.artist_list
  context.playlist = mpd.playlist
  mpd.updateArtistList()
  mpd.updatePlaylist()

  render()

  $("#line").keydown (event) ->
    if event.keyCode == 13
      line = $("#line").val()
      $("#line").val('')
      mpd.sendCommand line, (msg) ->
        $("#text").val(msg)


