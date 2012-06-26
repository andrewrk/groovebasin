exports.Plugin = class
  constructor: (@log, @onStateChanged, @onStatusChanged) ->
    @mpd = null
    @conf = null
    @is_enabled = true
  saveState: (state) =>
  restoreState: (state) =>
  handleRequest: (request, response) => false
  setConf: (conf, conf_path) =>
  setMpd: (mpd) =>
  onSocketConnection: (socket, getPermissions) =>
  onSendStatus: (status) =>
