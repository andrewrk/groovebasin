fs = require 'fs'
http = require 'http'
net = require 'net'
socketio = require 'socket.io'
static = require 'node-static'
formidable = require 'formidable'
url = require 'url'
util = require 'util'
mpd = require './lib/mpd'

public_dir = "./public"
status =
  dynamic_mode: null # null -> disabled
  random_ids: {}
  stream_httpd_port: null
  upload_enabled: false
  download_enabled: false
  users: []
next_user_id = 0
stickers_enabled = false
mpd_conf = null

# static server
fileServer = new (static.Server) public_dir
app = http.createServer((request, response) ->
  parsed_url = url.parse(request.url)
  if parsed_url.pathname == '/upload' and request.method == 'POST'
    if status.upload_enabled
      form = new formidable.IncomingForm()
      form.parse request, (err, fields, file) ->
        moveFile file.qqfile.path, "#{mpd_conf.music_directory}/#{file.qqfile.name}"
        response.writeHead 200, {'content-type': 'text/html'}
        response.end JSON.stringify {success: true}
    else
      response.writeHead 500, {'content-type': 'text/plain'}
      response.end JSON.stringify {success: false, reason: "Uploads not enabled"}
  else
    request.addListener 'end', ->
      fileServer.serve request, response
).listen(process.env.npm_package_config_port)
io = socketio.listen(app)
io.set 'log level', process.env.npm_package_config_log_level
log = io.log
log.info "Serving at http://localhost:#{process.env.npm_package_config_port}/"

# read mpd conf
do ->
  try
    data = fs.readFileSync(process.env.npm_package_config_mpd_conf)
  catch error
    log.warn "Unable to read mpd conf file: #{error}"
    return
  mpd_conf = require('./lib/mpdconf').parse(data.toString())
  if mpd_conf.music_directory?
    status.upload_enabled = true
    status.download_enabled = true
  else
    log.warn "music directory not found in mpd conf"
  if not (status.stream_httpd_port = mpd_conf.audio_output?.httpd?.port)?
    log.warn "httpd streaming not enabled in mpd conf"
  if mpd_conf.sticker_file?
    # changing from null to false, enables but does not turn on dynamic mode
    status.dynamic_mode = false
    stickers_enabled = true
  else
    log.warn "sticker_file not set in mpd conf"
  if mpd_conf.auto_update isnt "yes"
    log.warn "recommended to turn auto_update on in mpd conf"
  if mpd_conf.gapless_mp3_playback isnt "yes"
    log.warn "recommended to turn gapless_mp3_playback on in mpd conf"
  if mpd_conf.volume_normalization isnt "yes"
    log.warn "recommended to turn volume_normalization on in mpd conf"
  if isNaN(n = parseInt(mpd_conf.max_command_list_size)) or n < 16384
    log.warn "recommended to set max_command_list_size to >= 16384 in mpd conf"

# set up library link
if mpd_conf?.music_directory?
  library_link = "#{public_dir}/library"
  try fs.unlinkSync library_link
  ok = true
  do ->
    try
      fs.symlinkSync mpd_conf.music_directory, library_link
    catch error
      log.warn "Unable to link public/library to #{mpd_conf.music_directory}: #{error}"
      status.upload_enabled = false
      status.download_enabled = false
      return
    try
      fs.readdirSync library_link
    catch error
      status.upload_enabled = false
      status.download_enabled = false
      err? and log.warn "Unable to access music directory: #{error}"

moveFile = (source, dest) ->
  in_stream = fs.createReadStream(source)
  out_stream = fs.createWriteStream(dest)
  util.pump in_stream, out_stream, -> fs.unlink source

createMpdConnection = (cb) ->
  net.connect mpd_conf?.port ? 6600, mpd_conf?.bind_to_address ? "localhost", cb

sendStatus = ->
  my_mpd.sendCommand "sendmessage Status #{JSON.stringify JSON.stringify status}"

setDynamicMode = (value) ->
  # return if dynamic mode is disabled
  return unless status.dynamic_mode?
  return if status.dynamic_mode == value
  status.dynamic_mode = value
  checkDynamicMode()
  sendStatus()
previous_ids = {}
checkDynamicMode = ->
  return unless stickers_enabled
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

io.sockets.on 'connection', (socket) ->
  user_id = "user_" + next_user_id
  next_user_id += 1
  status.users.push user_id
  mpd_socket = createMpdConnection ->
    log.debug "browser to mpd connect"
  mpd_socket.on 'data', (data) ->
    socket.emit 'FromMpd', data.toString()
  mpd_socket.on 'end', ->
    log.debug "browser mpd disconnect"
    try socket.emit 'disconnect'
  mpd_socket.on 'error', ->
    log.debug "browser no mpd daemon found."

  socket.on 'ToMpd', (data) ->
    log.debug "[in] " + data
    try mpd_socket.write data
  socket.on 'DynamicMode', (data) ->
    log.debug "DynamicMode is being turned #{data.toString()}"
    value = JSON.parse data.toString()
    setDynamicMode value

  socket.on 'disconnect', ->
    mpd_socket.end()
    status.users = (id for id in status.users when id != user_id)
    sendStatus()

# our own mpd connection
class DirectMpd extends mpd.Mpd
  constructor: (@mpd_socket) ->
    super()
    @mpd_socket.on 'data', (data) =>
      @receive data.toString()
    @mpd_socket.on 'end', ->
      log.warn "server mpd disconnect"
    @mpd_socket.on 'error', ->
      log.warn "server no mpd daemon found."
    # whenever anyone joins, send status to everyone
    @updateFuncs.subscription = ->
      sendStatus()
    @updateFuncs.sticker = ->
      updateStickers()

  rawSend: (data) =>
    try @mpd_socket.write data

my_mpd = null
my_mpd_socket = createMpdConnection ->
  log.debug "server to mpd connect"
  my_mpd.handleConnectionStart()
my_mpd = new DirectMpd(my_mpd_socket)
my_mpd.on 'error', (msg) ->
  log.error msg
my_mpd.on 'statusupdate', checkDynamicMode
my_mpd.on 'playlistupdate', checkDynamicMode
my_mpd.on 'libraryupdate', updateStickers

# downgrade user permissions
try process.setuid uid if (uid = process.env.npm_package_config_user_id)?
