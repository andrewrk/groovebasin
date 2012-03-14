exports.Plugin = class Plugin
  constructor: ->
    @mpd = null
    @conf = null
    @is_enabled = true
    # whoever initializes this Plugin shall set @log to a logger.
    @log = null
    # whoever initializes this Plugin shall set @onStateChanged to a callback.
    @onStateChanged = null
    # whoever initializes this Plugin shall set @onStatusChanged to a callback.
    @onStatusChanged = null
  saveState: (state) =>
  restoreState: (state) =>
  handleRequest: (request, response) => false
  setConf: (conf, conf_path) =>
  setMpd: (mpd) =>
  onSocketConnection: (socket) =>
  onSendStatus: (status) =>
