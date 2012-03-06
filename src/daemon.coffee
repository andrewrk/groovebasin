fs = require 'fs'
http = require 'http'
net = require 'net'
socketio = require 'socket.io'
static = require 'node-static'
formidable = require 'formidable'
url = require 'url'
util = require 'util'
mpd = require './lib/mpd'
LastFmNode = require('lastfm').LastFmNode

lastfm = new LastFmNode
  api_key: process.env.npm_package_config_lastfm_api_key
  secret: process.env.npm_package_config_lastfm_secret

public_dir = "./public"
state =
  state_version: 1 # bump this whenever persistent state should be discarded
  next_user_id: 0
  lastfm_scrobblers: {}
  scrobbles: []
  status: # this structure is visible to clients
    dynamic_mode: null # null -> disabled
    random_ids: {}
    stream_httpd_port: null
    upload_enabled: false
    download_enabled: false
    users: []
    user_names: {}
    chats: []
    lastfm_api_key: process.env.npm_package_config_lastfm_api_key

do ->
  try
    loaded_state = JSON.parse fs.readFileSync process.env.npm_package_config_state_file, "utf8"
  return unless loaded_state?.state_version == state.state_version
  state = loaded_state
  # the online users list is always blank at startup
  state.status.users = []
  # always use the config value for lastfm_api_key
  state.status.lastfm_api_key = process.env.npm_package_config_lastfm_api_key
stickers_enabled = false
mpd_conf = null

# static server
fileServer = new (static.Server) public_dir
app = http.createServer((request, response) ->
  parsed_url = url.parse(request.url)
  if parsed_url.pathname == '/upload' and request.method == 'POST'
    if state.status.upload_enabled
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
    state.status.upload_enabled = true
    state.status.download_enabled = true
  else
    log.warn "music directory not found in mpd conf"
  if not (state.status.stream_httpd_port = mpd_conf.audio_output?.httpd?.port)?
    log.warn "httpd streaming not enabled in mpd conf"
  if mpd_conf.sticker_file?
    # changing from null to false, enables but does not turn on dynamic mode
    state.status.dynamic_mode = false if state.status.dynamic_mode == null
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
      state.status.upload_enabled = false
      state.status.download_enabled = false
      return
    try
      fs.readdirSync library_link
    catch error
      state.status.upload_enabled = false
      state.status.download_enabled = false
      err? and log.warn "Unable to access music directory: #{error}"

moveFile = (source, dest) ->
  in_stream = fs.createReadStream(source)
  out_stream = fs.createWriteStream(dest)
  util.pump in_stream, out_stream, -> fs.unlink source

createMpdConnection = (cb) ->
  net.connect mpd_conf?.port ? 6600, mpd_conf?.bind_to_address ? "localhost", cb

sendStatus = ->
  my_mpd.sendCommand "sendmessage Status #{JSON.stringify JSON.stringify state.status}"
  saveState()

saveState = ->
  fs.writeFile process.env.npm_package_config_state_file, JSON.stringify(state), "utf8"

setDynamicMode = (value) ->
  # return if dynamic mode is disabled
  return unless state.status.dynamic_mode?
  return if state.status.dynamic_mode == value
  state.status.dynamic_mode = value
  checkDynamicMode()
  sendStatus()
previous_ids = {}
checkDynamicMode = ->
  return unless stickers_enabled
  return unless my_mpd.status?.current_item?
  item_list = my_mpd.playlist.item_list
  current_id = my_mpd.status.current_item.id
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
      delete state.status.random_ids[item_list[i].id]

  if state.status.dynamic_mode
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
        state.status.random_ids[value] = 1
        changed = true
      sendStatus() if changed

  # scrub the random_ids
  new_random_ids = {}
  for id of state.status.random_ids
    if all_ids[id]
      new_random_ids[id] = 1
  state.status.random_ids = new_random_ids
  previous_ids = all_ids
  sendStatus()

scrubStaleUserNames = ->
  keep_user_ids = {}
  for user_id in state.status.users
    keep_user_ids[user_id] = true
  for chat_object in state.status.chats
    keep_user_ids[chat_object.user_id] = true
  log.debug "keep_ids #{(copy for copy of keep_user_ids)}"
  for user_id of state.status.user_names
    delete state.status.user_names[user_id] unless keep_user_ids[user_id]
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
        value = mpd.split_once(value, "=")[1]
        track = my_mpd.library.track_table[current_file]
        if track?
          track.last_queued = new Date(value)
        else
          log.error "#{current_file} has a last-queued sticker of #{value} but we don't have it in our library cache."

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

flushScrobbleQueue = ->
  log.debug "flushing scrobble queue"
  max_simultaneous = 10
  count = 0
  while (params = state.scrobbles.shift())? and count++ < max_simultaneous
    log.info "scrobbling #{params.track} for session #{params.sk}"
    params.handlers =
      error: (error) ->
        log.error "error from last.fm track.scrobble: #{error.message}"
        if not error?.code? or error.code is 11 or error.code is 16
          # retryable - add to queue
          state.scrobbles.push params
          saveState()
    lastfm.request 'track.scrobble', params
  saveState()

queueScrobble = (params) ->
  state.scrobbles.push params
  saveState()

last_playing_item = null
playing_start = new Date()
playing_time = 0
previous_play_state = null
checkScrobble = ->
  this_item = my_mpd.status.current_item

  if my_mpd.status.state is 'play'
    if previous_play_state isnt 'play'
      playing_start = new Date(new Date().getTime() - playing_time)
      previous_play_state = my_mpd.status.state
  playing_time = new Date().getTime() - playing_start.getTime()
  log.debug "playtime so far: #{playing_time}"

  if this_item?.id isnt last_playing_item?.id
    log.debug "ids are different"
    if (track = last_playing_item?.track)?
      # then scrobble it
      min_amt = 15 * 1000
      max_amt = 4 * 60 * 1000
      half_amt = track.time / 2 * 1000
      if playing_time >= min_amt and (playing_time >= max_amt or playing_time >= half_amt)
        if track.artist_name
          for username, session_key of state.lastfm_scrobblers
            log.debug "queuing scrobble: #{track.name} for #{username}"
            queueScrobble
              sk: session_key
              timestamp: Math.round(playing_start.getTime() / 1000)
              album: track.album?.name or ""
              track: track.name or ""
              artist: track.artist_name or ""
              albumArtist: track.album_artist_name or ""
              duration: track.time or ""
              trackNumber: track.track or ""
          flushScrobbleQueue()
        else
          log.warn "Not scrobbling #{track.name} - missing artist."

    last_playing_item = this_item
    previous_play_state = my_mpd.status.state
    playing_start = new Date()
    playing_time = 0

previous_now_playing_id = null
updateNowPlaying = ->
  return unless my_mpd.status.state is 'play'
  return unless (track = my_mpd.status.current_item?.track)?

  return unless previous_now_playing_id isnt my_mpd.status.current_item.id
  previous_now_playing_id = my_mpd.status.current_item.id

  if not track.artist_name
    log.warn "Not updating last.fm now playing for #{track.name}: missing artist"
    return

  for username, session_key of state.lastfm_scrobblers
    log.debug "update now playing with session_key: #{session_key}, track: #{track.name}, artist: #{track.artist_name}, album: #{track.album?.name}"
    lastfm.request "track.updateNowPlaying",
      sk: session_key
      track: track.name or ""
      artist: track.artist_name or ""
      album: track.album?.name or ""
      albumArtist: track.album_artist_name or ""
      trackNumber: track.track or ""
      duration: track.time or ""
      handlers:
        error: (error) ->
          log.error "error from last.fm track.updateNowPlaying: #{error.message}"

io.sockets.on 'connection', (socket) ->
  user_id = "user_" + state.next_user_id
  state.next_user_id += 1
  state.status.users.push user_id
  socket.emit 'Identify', user_id
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
    log.debug "[in] #{data}"
    try mpd_socket.write data
  socket.on 'DynamicMode', (data) ->
    value = JSON.parse data.toString()
    log.debug "DynamicMode is being turned #{value}"
    setDynamicMode value
  socket.on 'Chat', (data) ->
    chat_object =
      user_id: user_id
      message: data.toString()
    chats = state.status.chats
    chats.push(chat_object)
    chats_limit = 20
    chats.splice(0, chats.length - chats_limit) if chats.length > chats_limit
    sendStatus()
  socket.on 'SetUserName', (data) ->
    user_name = data.toString().trim().split(/\s+/).join(" ")
    if user_name != ""
      user_name_limit = 20
      user_name = user_name.substr(0, user_name_limit)
      state.status.user_names[user_id] = user_name
    else
      delete state.status.user_names[user_id]
    sendStatus()
  socket.on 'LastfmGetSession', (data) ->
    log.debug "getting session with #{data}"
    lastfm.request "auth.getSession",
      token: data.toString()
      handlers:
        success: (data) ->
          # clear them from the scrobblers
          delete state.lastfm_scrobblers[data?.session?.name]
          socket.emit 'LastfmGetSessionSuccess', JSON.stringify(data)
          log.debug "success from last.fm auth.getSession: #{JSON.stringify data}"
        error: (error) ->
          log.error "error from last.fm auth.getSession: #{error.message}"
          socket.emit 'LastfmGetSessionError', JSON.stringify(error)
  socket.on 'LastfmScrobblersAdd', (data) ->
    data_str = data.toString()
    log.debug "LastfmScrobblersAdd: #{data_str}"
    params = JSON.parse(data_str)
    # ignore if scrobbling user already exists. this is a fake request.
    return if state.lastfm_scrobblers[params.username]?
    state.lastfm_scrobblers[params.username] = params.session_key
    saveState()
  socket.on 'LastfmScrobblersRemove', (data) ->
    params = JSON.parse(data.toString())
    session_key = state.lastfm_scrobblers[params.username]
    if session_key is params.session_key
      delete state.lastfm_scrobblers[params.username]
      saveState()
    else
      log.warn "Invalid session key from user trying to remove scrobbler: #{params.username}"
    
  socket.on 'disconnect', ->
    mpd_socket.end()
    state.status.users = (id for id in state.status.users when id != user_id)
    scrubStaleUserNames()

# our own mpd connection
class DirectMpd extends mpd.Mpd
  constructor: (@mpd_socket) ->
    super()
    @mpd_socket.on 'data', (data) =>
      @receive data
    @mpd_socket.on 'end', ->
      log.warn "server mpd disconnect"
    @mpd_socket.on 'error', ->
      log.warn "server no mpd daemon found."
    # whenever anyone joins, send status to everyone
    @updateFuncs.subscription = ->
      sendStatus()
    @updateFuncs.sticker = ->
      updateStickers()
    @user_id = "[server]"

  rawSend: (data) =>
    try @mpd_socket.write data


my_mpd = null
my_mpd_socket = createMpdConnection ->
  log.debug "server to mpd connect"
  my_mpd.handleConnectionStart()
my_mpd = new DirectMpd(my_mpd_socket)
my_mpd.on 'error', (msg) ->
  log.error msg
my_mpd.on 'statusupdate', ->
  checkDynamicMode()
  updateNowPlaying()
  checkScrobble()
my_mpd.on 'playlistupdate', checkDynamicMode
my_mpd.on 'libraryupdate', updateStickers
my_mpd.on 'chat', scrubStaleUserNames


# every 2 minutes, flush scrobble queue
setTimeout flushScrobbleQueue, 120000

# downgrade user permissions
try process.setuid uid if (uid = process.env.npm_package_config_user_id)?
