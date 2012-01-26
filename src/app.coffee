context =
  playing: -> this.status?.state == 'play'

mpd = null
base_title = document.title

renderPlaylist = ->
  window._debug_context = context
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
  track = context.status.current_item.track
  document.title = "#{track.name} - #{track.artist.name} - #{track.album.name} - #{base_title}"

  $nowplaying = $("#nowplaying")
  $nowplaying.html Handlebars.templates.playback(context)

  $nowplaying.find('.pause a').click (event) ->
    mpd.pause()
    return false

  $nowplaying.find('.stop a').click (event) ->
    mpd.stop()
    return false

  $nowplaying.find('.play a').click (event) ->
    mpd.play()
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
  mpd.onStatusUpdate renderNowPlaying
  context.artists = mpd.library.artist_list
  context.playlist = mpd.playlist.item_list
  context.status = mpd.status
  mpd.updateArtistList()
  mpd.updatePlaylist()

  render()

  $("#line").keydown (event) ->
    if event.keyCode == 13
      line = $("#line").val()
      $("#line").val('')
      mpd.sendCommand line, (msg) ->
        $("#text").val(msg)


