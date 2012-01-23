context = {}
mpd = null

render = ->
  $nowplaying = $("#nowplaying")
  $library = $("#library")
  $queue = $("#queue")

  $nowplaying.html Handlebars.templates.playback(context)
  $queue.html Handlebars.templates.playlist(context)
  $library.html Handlebars.templates.library(context)

  $library.find('a.artist').click (event) ->
    artist_name = $(event.target).text()
    mpd.getAlbumsForArtist artist_name, (albums) ->
      context.artist_table[artist_name].albums = albums
      render()
    return false

  $library.find('a.track').click (event) ->
    file = $(event.target).data('file')
    mpd.queueTrack file
    return false

  $nowplaying.find('.pause a').click (event) ->
    mpd.pause()
    return false

  $nowplaying.find('.play a').click (event) ->
    mpd.play()
    return false

  $queue.find('a.track').click (event) ->
    track_id = $(event.target).data('id')
    mpd.playId track_id
    return false

$(document).ready ->
  mpd = new Mpd()

  Handlebars.registerHelper 'hash', (context, options) ->
    ret = ""
    for k,v of context
      ret += options.fn $.extend({key: k, val: v}, options.fn(context))
    ret

  render()

  mpd.getArtistList (artist_names) ->
    context.artists = []
    context.artist_table = {}
    for artist in artist_names
      obj = {name: artist}
      context.artists.push obj
      context.artist_table[artist] = obj
    render()

    mpd.getPlaylist (playlist) ->
      context.playlist = playlist
      render()


  $("#line").keydown (event) ->
    if event.keyCode == 13
      line = $("#line").val()
      $("#line").val('')
      mpd.sendCommand line, (msg) ->
        $("#text").val(msg)


