#!/usr/bin/env coffee

# default configuration options. Leave this as valid JSON, not coffee-script;
# we're using it as an example for the ~/.groovebasinrc file.
# (leave off the "default_config = " in your config file)
default_config = {
  "user_id": "mpd",
  "log_level": 1,
  "http": {
    "port": 10000
  },
  "mpd": {
    "host": "localhost",
    "port": 6600,
    "conf": "/etc/mpd.conf",
    "stream_url": "http://localhost:8000/mpd.ogg"
  }
}

fs = require 'fs'
http = require 'http'
net = require 'net'
socketio = require 'socket.io'
static = require 'node-static'
nconf = require 'nconf'
formidable = require 'formidable'
url = require 'url'
util = require 'util'
mpd = require './lib/mpd'

nconf
  .argv()
  .env()
  .file({file: "#{process.env.HOME}/.groovebasinrc"})
  .defaults(default_config)

# read mpd conf
music_directory = null
fs.readFile nconf.get('mpd:conf'), (err, data) ->
  if err
    console.log "Error reading mpd conf file: #{err}"
    return
  m = data.toString().match /^music_directory\s+"(.*)"$/m
  music_directory = m[1] if m?
  if music_directory?
    console.log "Music directory: #{music_directory}"
  else
    console.log "ERROR - music directory not found"

# check for library link
public_dir = "./public"
library_link = public_dir + "/library"
fs.readdir library_link, (err, files) ->
  err? and console.log "ERROR: #{library_link} not linked to media library"

moveFile = (source, dest) ->
  in_stream = fs.createReadStream(source)
  out_stream = fs.createWriteStream(dest)
  util.pump in_stream, out_stream, -> fs.unlink source

# static server
fileServer = new (static.Server) './public'
app = http.createServer((request, response) ->
  parsed_url = url.parse(request.url)
  if parsed_url.pathname == '/upload' and request.method == 'POST'
    form = new formidable.IncomingForm()
    form.parse request, (err, fields, file) ->
      moveFile file.qqfile.path, "#{music_directory}/#{file.qqfile.name}"
      response.writeHead 200, {'content-type': 'text/html'}
      response.end JSON.stringify {success: true}
  else
    request.addListener 'end', ->
      fileServer.serve request, response
).listen(nconf.get('http:port'))
console.log "Attempting to serve http://localhost:#{nconf.get('http:port')}/"

createMpdConnection = (cb) ->
  net.connect nconf.get('mpd:port'), nconf.get('mpd:host'), cb

status =
  dynamic_mode: false
  random_ids: {}
  stream_url: nconf.get('mpd:stream_url')
sendStatus = ->
  my_mpd.sendCommand "sendmessage Status #{JSON.stringify JSON.stringify status}"

setDynamicMode = (value) ->
  return if status.dynamic_mode == value
  status.dynamic_mode = value
  checkDynamicMode()
  sendStatus()
previous_ids = {}
checkDynamicMode = ->
  item_list = my_mpd.playlist.item_list
  current_id = my_mpd.status?.current_item?.id
  current_index = -1
  all_ids = {}
  new_files = []
  for item, i in item_list
    if item.id == current_id
      current_index = i
    all_ids[item.id] = true
    if not previous_ids[item.id]?
      new_files.push item.track.file
  # tag any newly queued tracks
  my_mpd.sendCommands ("sticker set song \"#{file}\" \"#{sticker_name}\" #{JSON.stringify new Date()}" for file in new_files)
  # anticipate the changes
  my_mpd.library.track_table[file].last_queued = new Date() for file in new_files
  # if no track is playing, assume the first track is about to be
  if current_index == -1
    current_index = 0
  else
    # any tracks <= current track don't count as random anymore
    for i in [0..current_index]
      delete status.random_ids[item_list[i].id]

  if status.dynamic_mode
    commands = []
    delete_count = Math.max(current_index - 10, 0)
    for i in [0...delete_count]
      commands.push "deleteid #{item_list[i].id}"
    add_count = Math.max(11 - (item_list.length - current_index), 0)

    commands = commands.concat ("addid #{JSON.stringify file}" for file in getRandomSongFiles add_count)
    my_mpd.sendCommands commands, (msg) ->
      # track which ones are the automatic ones
      changed = false
      for line in msg.split("\n")
        [name, value] = line.split(": ")
        continue if name != "Id"
        status.random_ids[value] = 1
        changed = true
      sendStatus() if changed

  # scrub the random_ids
  new_random_ids = {}
  for id of status.random_ids
    if all_ids[id]
      new_random_ids[id] = 1
  status.random_ids = new_random_ids
  previous_ids = all_ids
  sendStatus()

sticker_name = "groovebasin.last-queued"
updateStickers = ->
  my_mpd.sendCommand "sticker find song \"/\" \"#{sticker_name}\"", (msg) ->
    current_file = null
    for line in msg.split("\n")
      [name, value] = mpd.split_once line, ": "
      if name == "file"
        current_file = value
      else if name == "sticker"
        [_, value] = mpd.split_once value, "="
        my_mpd.library.track_table[current_file].last_queued = new Date(value)

getRandomSongFiles = (count) ->
  return [] if count == 0
  never_queued = []
  sometimes_queued = []
  for _, track of my_mpd.library.track_table
    if track.last_queued?
      sometimes_queued.push track
    else
      never_queued.push track
  # backwards by time
  sometimes_queued.sort (a, b) ->
    b.last_queued.getTime() - a.last_queued.getTime()
  # distribution is a triangle for ever queued, and a rectangle for never queued
  #    ___
  #   /| |
  #  / | |
  # /__|_|
  max_weight = sometimes_queued.length
  triangle_area = Math.floor(max_weight * max_weight / 2)
  rectangle_area = max_weight * never_queued.length
  total_size = triangle_area + rectangle_area
  # decode indexes through the distribution shape
  files = []
  for i in [0...count]
    index = Math.random() * total_size
    if index < triangle_area
      # triangle
      track = sometimes_queued[Math.floor Math.sqrt index]
    else
      # rectangle
      track = never_queued[Math.floor((index - triangle_area) / max_weight)]
    files.push track.file
  files

io = socketio.listen(app)
io.set 'log level', nconf.get('log_level')
io.sockets.on 'connection', (socket) ->
  mpd_socket = createMpdConnection ->
    console.log "browser to mpd connect"
  mpd_socket.on 'data', (data) ->
    socket.emit 'FromMpd', data.toString()
  mpd_socket.on 'end', ->
    console.log "browser mpd disconnect"
    try socket.emit 'disconnect'
  mpd_socket.on 'error', ->
    console.log "browser no mpd daemon found."

  socket.on 'ToMpd', (data) ->
    console.log "[in] " + data
    try mpd_socket.write data
  socket.on 'DynamicMode', (data) ->
    console.log "DynamicMode is being turned #{data.toString()}"
    value = JSON.parse data.toString()
    setDynamicMode value

  socket.on 'disconnect', -> mpd_socket.end()

# our own mpd connection
class DirectMpd extends mpd.Mpd
  constructor: (@mpd_socket) ->
    super()
    @mpd_socket.on 'data', (data) =>
      @receive data.toString()
    @mpd_socket.on 'end', ->
      console.log "server mpd disconnect"
    @mpd_socket.on 'error', ->
      console.log "server no mpd daemon found."
    # whenever anyone joins, send status to everyone
    @updateFuncs.subscription = ->
      sendStatus()
    @updateFuncs.sticker = ->
      updateStickers()

  rawSend: (data) =>
    try @mpd_socket.write data

my_mpd = null
my_mpd_socket = createMpdConnection ->
  console.log "server to mpd connect"
  my_mpd.handleConnectionStart()
my_mpd = new DirectMpd(my_mpd_socket)
my_mpd.on 'error', (msg) ->
  console.log "ERROR: " + msg
my_mpd.on 'statusupdate', checkDynamicMode
my_mpd.on 'playlistupdate', checkDynamicMode
my_mpd.on 'libraryupdate', updateStickers

# downgrade user permissions
uid = nconf.get('user_id')
try process.setuid uid if uid?
