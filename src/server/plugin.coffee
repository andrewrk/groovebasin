exports.Plugin = class
  constructor: (@onStateChanged, @onStatusChanged) ->
    @mpd = null
    @conf = null
    @is_enabled = true
  saveState: (state) =>
  restoreState: (state) =>
  setConf: (conf, conf_path) =>
  setMpd: (mpd) =>
  setUpRoutes: (app) =>
  onSocketConnection: (socket, getPermissions) =>
  onSendStatus: (status) =>
