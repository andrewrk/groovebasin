#!/usr/bin/env coffee

fs = require 'fs'
http = require 'http'
net = require 'net'
socketio = require 'socket.io'
static = require 'node-static'
nconf = require 'nconf'
formidable = require 'formidable'
url = require 'url'
util = require 'util'

class DirectMpd extends require('./lib/mpd').Mpd
  constructor: (@mpd_socket) ->
    super()
    @mpd_socket.on 'data', (data) =>
      @receive data.toString()
    @mpd_socket.on 'end', ->
      console.log "server mpd disconnect"
    @mpd_socket.on 'error', ->
      console.log "server no mpd daemon found."

  send: (data) =>
    try @mpd_socket.write data

nconf
  .argv()
  .env()
# these files in the wrong order because of
# https://github.com/flatiron/nconf/issues/28
  .file({file: "/etc/groovebasinrc"})
  .file({file: "#{process.env.HOME}/.groovebasinrc"})
  .defaults
    user_id: "mpd"
    log_level: 1
    http:
      port: 80
    mpd:
      host: 'localhost'
      port: 6600
      conf: "/etc/mpd.conf"

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
send_status = (socket) ->
  socket.emit 'Status', JSON.stringify(status)

set_dynamic_mode = (value) ->
  return if status.dynamic_mode == value
  status.dynamic_mode = value
  check_dynamic_mode()
  # TODO: push a status update to all connected sockets
check_dynamic_mode = ->
  return if not status.dynamic_mode
  item_list = my_mpd.playlist.item_list
  current_id = my_mpd.status?.current_item?.id
  # if no track is playing, assume the first track is about to be
  current_index = 0
  for item, i in item_list
    if item.id == current_id
      current_index = i
      break
  commands = []
  delete_count = Math.max(current_index - 10, 0)
  for i in [0...delete_count]
    commands.push "deleteid #{item_list[i].id}"
  add_count = Math.max(11 - (item_list.length - current_index), 0)
  commands = commands.concat my_mpd.queueRandomTracksCommands add_count
  my_mpd.sendCommands commands

io = socketio.listen(app)
io.set 'log level', nconf.get('log_level')
io.sockets.on 'connection', (socket) ->
  mpd_socket = createMpdConnection ->
    console.log "browser to mpd connect"
    send_status socket
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
    if !(value == true || value == false)
      console.log "ERROR: wtf #{data.toString()}"
      return
    set_dynamic_mode value

  socket.on 'disconnect', -> mpd_socket.end()

# our own mpd connection
my_mpd = null
my_mpd_socket = createMpdConnection ->
  console.log "server to mpd connect"
  my_mpd.handleConnectionStart()
my_mpd = new DirectMpd(my_mpd_socket)
my_mpd.on 'statusupdate', check_dynamic_mode
my_mpd.on 'playlistupdate', check_dynamic_mode

# downgrade user permissions
uid = nconf.get('user_id')
try process.setuid uid if uid?
