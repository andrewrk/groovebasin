http = require 'http'
net = require 'net'
socketio = require 'socket.io'
static = require 'node-static'

config =
  log_level: 3
  http:
    port: 7777
  mpd:
    host: 'localhost'
    port: 6600

client = null
onMpdData = null
connectMpdClient = ->
  client = net.connect config.mpd.port, config.mpd.host, ->
    console.log 'client connected'
    client.on 'data', onMpdData if onMpdData?
  client.on 'end', ->
    console.log 'client disconnected, reconnecting'
    connectMpdClient()

connectMpdClient()


fileServer = new (static.Server) './public'
app = http.createServer((request, response) ->
  request.addListener 'end', ->
    fileServer.serve request, response

).listen(config.http.port)

io = socketio.listen(app)
io.set 'log level', config.log_level
io.sockets.on 'connection', (socket) ->
  socket.on 'ToMpd', (data) ->
    console.log "[in] " + data
    client.write data

  onMpdData = (data) ->
    socket.emit 'FromMpd', data.toString()

  client.on 'data', onMpdData
