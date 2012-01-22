context = {}

render = ->
  $("#nowplaying").html Handlebars.templates.playback(context)
  $("#library").html Handlebars.templates.library(context)

$(document).ready ->
  render()

  mpd = new Mpd()

  mpd.getArtistList (artists) ->
    context.artists = ({name: artist} for artist in artists)
    render()

  $("#line").keydown (event) ->
    if event.keyCode == 13
      line = $("#line").val()
      $("#line").val('')
      mpd.send line

