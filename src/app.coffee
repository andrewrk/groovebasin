$(document).ready ->
  $("#nowplaying").html Mustache.render($("#view-nowplaying").html(), {})
