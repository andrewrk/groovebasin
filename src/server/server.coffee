fs = require 'fs'
http = require 'http'
net = require 'net'
socketio = require 'socket.io'
static = require 'node-static'
mpd = require './lib/mpd'
extend = require 'node.extend'


fileServer = new (static.Server) "./public"
app = http.createServer((request, response) ->
  return if plugins.handleRequest(request, response)
  request.addListener 'end', ->
    fileServer.serve request, response
).listen(process.env.npm_package_config_port)
io = socketio.listen(app)
io.set 'log level', process.env.npm_package_config_log_level
log = io.log
log.info "Serving at http://localhost:#{process.env.npm_package_config_port}/"


plugins =
  objects:
    lastfm: null
    dynamicmode: null
    upload: null
    download: null
    chat: null
    stream: null
  initialize: ->
    for name of this.objects
      plugin = this.objects[name] = new (require("./lib/plugins/#{name}").Plugin)()
      plugin.log = log
      plugin.onStateChanged = saveState
      plugin.onStatusChanged = ->
        saveState()
        sendStatus()
  call: (fn_name, args...) ->
    plugin[fn_name](args...) for name, plugin of this.objects
  handleRequest: (request, response) ->
    for name, plugin of this.objects
      return true if plugin.handleRequest(request, response)
    return false
  featuresList: ->
    ([name, plugin.is_enabled] for name, plugin of this.objects)

# state management
state =
  state_version: 2 # bump this whenever persistent state should be discarded
  status: {} # this structure is visible to clients

saveState = ->
  plugins.call "saveState", state
  fs.writeFile process.env.npm_package_config_state_file, JSON.stringify(state), "utf8"

restoreState = ->
  try loaded_state = JSON.parse fs.readFileSync process.env.npm_package_config_state_file, "utf8"
  if loaded_state?.state_version is state.state_version
    extend true, state, loaded_state
  # have the plugins restore and then save to delete values that should not
  # have been restored.
  plugins.call "restoreState", state
  plugins.call "saveState", state

sendStatus = ->
  for id, socket of all_sockets
    socket.emit 'Status', JSON.stringify state.status

plugins.initialize()
restoreState()

# read mpd conf
mpd_conf = null
do ->
  mpd_conf_path = process.env.npm_package_config_mpd_conf
  try
    data = fs.readFileSync(mpd_conf_path)
  catch error
    log.warn "Unable to read #{mpd_conf_path}: #{error}. Most features disabled."
    return
  mpd_conf = require('./lib/mpdconf').parse(data.toString())

  plugins.call "setConf", mpd_conf, mpd_conf_path

  if mpd_conf.auto_update isnt "yes"
    log.warn "recommended to turn auto_update on in #{mpd_conf_path}"
  if mpd_conf.gapless_mp3_playback isnt "yes"
    log.warn "recommended to turn gapless_mp3_playback on in #{mpd_conf_path}"
  if mpd_conf.volume_normalization isnt "yes"
    log.warn "recommended to turn volume_normalization on in #{mpd_conf_path}"
  if isNaN(n = parseInt(mpd_conf.max_command_list_size)) or n < 16384
    log.warn "recommended to set max_command_list_size to >= 16384 in #{mpd_conf_path}"

plugins.call "saveState", state

for [name, enabled] in plugins.featuresList()
  if enabled
    log.info "#{name} is enabled."
  else
    log.warn "#{name} is disabled."

createMpdConnection = (unix_socket, cb) ->
  if unix_socket and (path = mpd_conf?.bind_to_address?.unix_socket)?
    net.connect path, cb
  else
    port = mpd_conf?.port ? 6600
    host = mpd_conf?.bind_to_address?.network ? "localhost"
    net.connect port, host, cb

next_socket_id = 0
all_sockets = {}
io.sockets.on 'connection', (socket) ->
  this_socket_id = next_socket_id++
  all_sockets[this_socket_id] = socket

  mpd_socket = createMpdConnection false, ->
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
  socket.on 'disconnect', ->
    delete all_sockets[this_socket_id]
    mpd_socket.end()

  plugins.call "onSocketConnection", socket

# our own mpd connection
class DirectMpd extends mpd.Mpd
  constructor: (@mpd_socket) ->
    super()
    @mpd_socket.on 'data', @receive

  rawSend: (data) =>
    try @mpd_socket.write data


my_mpd = null
my_mpd_socket = createMpdConnection true, ->
  log.debug "server to mpd connect"
  my_mpd.handleConnectionStart()
my_mpd_socket.on 'end', ->
  log.warn "server mpd disconnect"
my_mpd_socket.on 'error', ->
  log.warn "server no mpd daemon found."
my_mpd = new DirectMpd(my_mpd_socket)
my_mpd.on 'error', (msg) -> log.error msg

plugins.call "setMpd", my_mpd

# downgrade user permissions
try process.setuid uid if (uid = process.env.npm_package_config_user_id)?
